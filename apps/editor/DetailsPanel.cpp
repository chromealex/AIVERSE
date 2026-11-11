// Inspector «Детали»: see DetailsPanel.h.
#include "DetailsPanel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include "EditorUtil.h"
#include "EditorWorldEdit.h"

namespace aiverse::editor {

namespace {
constexpr size_t kMaxItems = 300;   // objects whose data is read (the map fields cover the whole selection)
const char* const kMixed = "\x01mixed";

bool isMixed(const Json& v) { return v.isString() && v.str() == kMixed; }
bool isRef(const Json& v) { return v.isObject() && v.has("$entity"); }

// Common value of several JSON values: equal -> the value; objects / arrays -> merged member by member;
// otherwise the "mixed" marker.
Json merge(const std::vector<const Json*>& vs) {
    if (vs.empty()) return Json();
    bool same = true;
    for (size_t i = 1; i < vs.size() && same; ++i) same = *vs[i] == *vs[0];
    if (same) return *vs[0];
    bool objects = true, arrays = true;
    for (auto* v : vs) { objects &= v->isObject() && !isRef(*v); arrays &= v->isArray() && v->size() == vs[0]->size(); }
    if (objects) {
        Json out = Json::object();
        std::vector<std::string> keys;
        for (auto* v : vs) for (auto& [k, x] : v->members()) if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        for (auto& k : keys) {
            std::vector<const Json*> sub;
            bool all = true;
            for (auto* v : vs) { if (v->has(k)) sub.push_back(&(*v)[k]); else all = false; }
            out.set(k, all ? merge(sub) : Json::string(kMixed));
        }
        return out;
    }
    if (arrays && vs[0]->size() <= 64) {
        Json out = Json::array();
        for (size_t i = 0; i < vs[0]->size(); ++i) {
            std::vector<const Json*> sub;
            for (auto* v : vs) sub.push_back(&(*v)[i]);
            out.push(merge(sub));
        }
        return out;
    }
    return Json::string(kMixed);
}

std::string scalar(const Json& v) {
    if (isMixed(v)) return "—";
    if (v.isString()) return v.str();
    if (v.isNull()) return "нет";
    if (v.type() == Json::Type::Bool) return v.boolean() ? "да" : "нет";
    if (isRef(v)) return v.getStr("label");
    return v.dump(-1);
}

bool inlineArray(const Json& v) {
    if (!v.isArray() || v.size() > 6) return false;
    for (auto& x : v.items()) if (x.isObject() || x.isArray()) return false;
    return true;
}

std::string inlineText(const Json& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + scalar(v[i]);
    return s + "]";
}

ImVec4 ownerColor(int owner) {
    if (owner <= 0) return ImVec4(0, 0, 0, 0);
    const render::float4 c = game::RtsView::playerColor(owner);
    return ImVec4(c.x, c.y, c.z, 1);
}
} // namespace

void DetailsPanel::collect(Editor& ed) {
    // edit mode: the preview's rows (rebuilt with the map); play mode: the running world read directly for the
    // selected objects, again and again (one pass a few times a second, less often for big selections), whether
    // the Hierarchy is open or not. Reading has a time budget per frame; the sections are merged once a pass ends.
    if (!ed.worldLive()) ed.wantWorld();
    // edit mode: the data comes from the preview (its serial), valid while it matches the map (worldFresh)
    uint64_t key = ed.worldLive() ? 1ull << 62 : ed.world().serial * 1000003ull ^ uint64_t(ed.worldFresh()) << 61;
    for (auto& s : ed.selected()) key = (key ^ (uint64_t(s.kind) << 40 ^ uint64_t(uint32_t(s.index)) ^ s.ref)) * 1099511628211ull;
    if (key != cacheKey_) {
        cacheKey_ = key;
        items_.clear();
        truncated_ = 0;
        for (auto& s : ed.selected()) {
            if (s.kind != Selection::Entity && s.kind != Selection::Live) continue;
            Item it;
            it.sel = s;
            if (s.kind == Selection::Entity) it.doc = s.index;
            items_.push_back(std::move(it));
        }
        cursor_ = 0;
        dirty_ = true;
        sections_.clear();
        withData_ = 0;
    } else if (cursor_ >= items_.size() && ed.worldLive() && ed.time() >= refreshAt_) {
        cursor_ = 0;   // play: the next pass
    }
    if (cursor_ < items_.size()) {
        const auto t0 = std::chrono::steady_clock::now();
        size_t done = 0;
        for (; cursor_ < items_.size(); ++cursor_) {
            if (done > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 2.0) break;
            Item& it = items_[cursor_];
            const bool full = cursor_ < kMaxItems;
            it.hasRow = ed.worldDetails(it.sel, it.row, full ? &it.data : nullptr);
            if (!it.hasRow) it.data = Json();
            if (it.hasRow && it.doc < 0 && !ed.worldLive()) it.doc = it.row.doc;
            it.fresh = true;
            ++done;
        }
        dirty_ = true;
        if (cursor_ >= items_.size()) {
            truncated_ = items_.size() > kMaxItems ? int(items_.size() - kMaxItems) : 0;
            refreshAt_ = ed.time() + std::min(2.0, 0.2 * std::max<double>(1.0, double(std::min(items_.size(), kMaxItems)) / 25.0));
        }
    }
    // merge when a pass is complete, or the first time anything was read (a big selection shows up at once)
    if (dirty_ && (cursor_ >= items_.size() || sections_.empty())) { prepare(); dirty_ = false; }
}

void DetailsPanel::prepare() {
    sections_.clear();
    std::vector<const Item*> withData;
    for (auto& it : items_) if (it.fresh && it.data.isObject() && !it.data.has("error")) withData.push_back(&it);
    withData_ = withData.size();
    if (withData.empty()) return;
    // effective vs base stats: "base → effective" (one value when they agree)
    {
        std::vector<Json> flat;
        for (auto* it : withData) {
            if (!it->data.has("stats")) continue;
            Json f = Json::object();
            for (auto& [k, x] : it->data["stats"].members()) {
                const std::string b = x["base"].dump(-1), e = x["effective"].dump(-1);
                f.set(k, Json::string(b == e ? b : b + " → " + e));
            }
            flat.push_back(std::move(f));
        }
        std::vector<const Json*> v;
        for (auto& f : flat) v.push_back(&f);
        if (!v.empty()) sections_.push_back({"stats", "Характеристики (база → итог)", merge(v), v.size(), true});
    }
    // components in the engine's order
    static const char* const open[] = {"Transform", "Owner", "Unit", "Orders", "Production", "Construction", "Garrison", "Cargo", "ResourceStock", "ScriptData", "Boarded", "Transport"};
    for (auto& c : worldComponents()) {
        std::vector<const Json*> v;
        for (auto* it : withData) if (it->data["components"].has(c.id)) v.push_back(&it->data["components"][c.id]);
        if (v.empty()) continue;
        bool def = false;
        for (auto* o : open) def |= std::string(o) == c.id;
        sections_.push_back({c.id, c.title, merge(v), v.size(), def});
    }
    // the rules block of the type
    std::vector<const Json*> rules;
    for (auto* it : withData) if (it->data["rules"].isObject()) rules.push_back(&it->data["rules"]);
    if (!rules.empty()) sections_.push_back({"rules", "Правила типа (rules.json)", merge(rules), rules.size(), false});
}

void DetailsPanel::draw(Editor& ed) {
    auto prof = ed.prof.scope(EditorProfile::Details);   // all of it: reading + merging + drawing
    collect(ed);
    if (items_.empty()) { ui::emptyStateHere(ICON_INSPECTOR, "Объект исчез", "Его больше нет в мире игры."); return; }
    header(ed);
    const float clear = filter_.empty() ? 0 : ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    ui::searchField("##propfilter", filter_, "Фильтр свойств", std::max(40.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4 - clear));
    ImGui::SameLine(0, 4);
    if (ui::toolButton(ICON_BRACES, "Копировать всё как JSON")) {
        Json all = Json::array();
        for (auto& it : items_) {
            Json j = it.data.isObject() ? it.data : Json::object();
            if (it.hasRow) j.set("ref", Json::number(int64_t(it.row.ref)));
            if (it.doc >= 0 && !ed.worldLive() && size_t(it.doc) < ed.doc().data.entities.size()) j.set("map", ed.doc().toJson()["entities"][size_t(it.doc)]);
            all.push(j);
        }
        ImGui::SetClipboardText((items_.size() == 1 ? all[0] : all).dump().c_str());
        ed.status = "Скопировано в буфер обмена";
    }
    mapFields(ed);
    sections(ed);
}

void DetailsPanel::header(Editor& ed) {
    const auto& R = ed.rules();
    const auto& d = ed.doc().data;
    auto typeOf = [&](const Item& it) -> int {
        if (it.doc >= 0 && !ed.worldLive() && size_t(it.doc) < d.entities.size()) return R.findType(d.entities[size_t(it.doc)].type);
        return it.hasRow ? it.row.type : -1;
    };
    auto ownerOf = [&](const Item& it) {
        if (it.doc >= 0 && !ed.worldLive() && size_t(it.doc) < d.entities.size()) return d.entities[size_t(it.doc)].owner;
        return it.hasRow ? it.row.owner : 0;
    };
    auto ownerName = [&](int o) {
        if (o == 0) return std::string("Нейтральный");
        if (o < 0) return std::string("Враждебные монстры");
        if (o <= int(d.players.size())) return d.players[size_t(o - 1)].name;
        return o < int(ed.world().playerNames.size()) ? ed.world().playerNames[size_t(o)] : "Игрок " + std::to_string(o);
    };
    const Item& first = items_[0];
    if (items_.size() == 1) {
        const int t = typeOf(first);
        std::string title = first.hasRow ? worldRowName(ed.world(), R, &d, first.row) : t >= 0 ? R.types[size_t(t)].name : "Объект";
        if (first.doc >= 0 && !ed.worldLive() && size_t(first.doc) < d.entities.size()) {
            title = t >= 0 ? R.types[size_t(t)].name : d.entities[size_t(first.doc)].type;
            if (!d.entities[size_t(first.doc)].id.empty()) title += "  #" + d.entities[size_t(first.doc)].id;
        }
        std::string sub = ownerName(ownerOf(first));
        if (first.hasRow) sub += "  ·  " + std::string(worldOriginTitle(first.row.origin)) + "  ·  #" + std::to_string(first.row.e.index);
        if (ed.worldLive()) sub += "  ·  живой, только чтение";
        else if (first.doc < 0) sub += "  ·  только чтение";
        const bool structure = t >= 0 && R.types[size_t(t)].structure;
        ui::headerCard(first.hasRow && first.row.kind == WorldKind::Projectile ? ICON_ZAP : structure ? ICON_CASTLE : ICON_OBJECTS, title, sub, ownerColor(ownerOf(first)));
    } else {
        std::map<std::string, int> types;
        for (auto& it : items_) { const int t = typeOf(it); types[t >= 0 ? R.types[size_t(t)].name : "?"]++; }
        std::string sub;
        int shown = 0;
        for (auto& [n, c] : types) {
            if (shown++ == 3) { sub += ", …"; break; }
            sub += (sub.empty() ? "" : ", ") + n + " ×" + std::to_string(c);
        }
        ui::headerCard(ICON_LAYERS, "Выбрано объектов: " + std::to_string(items_.size()), sub);
    }
    // actions
    std::vector<int> docs;
    for (auto& it : items_) if (it.doc >= 0 && it.sel.kind == Selection::Entity) docs.push_back(it.doc);
    const int t0 = typeOf(first);
    bool oneType = t0 >= 0;
    for (auto& it : items_) oneType &= typeOf(it) == t0;
    const int n = 1 + (oneType ? 1 : 0) + (!docs.empty() && !ed.worldLive() ? 1 : 0);
    int k = 0;
    if (ui::wideButton(ICON_CROSSHAIR, "Показать", k++, n)) {
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (auto& it : items_) {
            float x, z;
            if (!ed.selectionPos(it.sel, x, z)) continue;
            x0 = std::min(x0, x); z0 = std::min(z0, z); x1 = std::max(x1, x); z1 = std::max(z1, z);
        }
        if (x1 >= x0) ed.focusScene((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, std::clamp(std::max(x1 - x0, z1 - z0) * 1.2f + 14, 14.0f, 200.0f));
    }
    if (oneType && ui::wideButton(ICON_RULES, "Правила", k++, n)) ed.select({Selection::Type, -1, R.types[size_t(t0)].id});
    if (!docs.empty() && !ed.worldLive() && ui::wideButton(ICON_TRASH, "Удалить", k++, n)) ed.removeSelectedEntities();
}

// The map document fields of the selected map entities (edit mode): shared value or "—"; edits apply to all.
void DetailsPanel::mapFields(Editor& ed) {
    if (ed.worldLive()) return;
    auto& doc = ed.doc();
    auto& d = doc.data;
    std::vector<int> idx;
    for (auto& it : items_) if (it.sel.kind == Selection::Entity && it.doc >= 0 && size_t(it.doc) < d.entities.size()) idx.push_back(it.doc);
    if (idx.empty()) return;
    const auto& R = ed.rules();
    const DocEntity& e0 = d.entities[size_t(idx[0])];
    bool sameType = true, sameOwner = true, sameGarrison = true, sameAmount = true;
    float cx = 0, cz = 0;
    for (int i : idx) {
        const DocEntity& e = d.entities[size_t(i)];
        sameType &= e.type == e0.type; sameOwner &= e.owner == e0.owner;
        sameGarrison &= e.garrison == e0.garrison; sameAmount &= e.amount == e0.amount;
        cx += e.x; cz += e.z;
    }
    cx /= float(idx.size()); cz /= float(idx.size());
    const int ti = R.findType(e0.type);
    if (!filter_.empty() && !ui::contains("тип владелец позиция гарнизон запас id карта", filter_)) return;
    ui::section(ICON_PENCIL, idx.size() > 1 ? "Карта (правка всех выбранных)" : "Карта (правка)");
    if (!ui::beginProps("##mapfields")) return;
    ui::prop("Тип", "Тип объекта из правил; меняется у всех выбранных (одна отмена)");
    if (ImGui::BeginCombo("##type", sameType ? (ti >= 0 ? R.types[size_t(ti)].name.c_str() : e0.type.c_str()) : "—", ImGuiComboFlags_HeightLarge)) {
        for (auto& t : R.types) if (ImGui::Selectable((t.name + "##" + t.id).c_str(), sameType && t.id == e0.type)) worldedit::setType(doc, idx, t.id);
        ImGui::EndCombo();
    }
    ui::prop("Владелец");
    auto ownerName = [&](int o) { return o == 0 ? std::string("Нейтральный") : o < 0 ? std::string("Враждебные монстры") : o <= int(d.players.size()) ? d.players[size_t(o - 1)].name : std::string("?"); };
    if (ImGui::BeginCombo("##owner", sameOwner ? ownerName(e0.owner).c_str() : "—")) {
        if (ImGui::Selectable("Нейтральный", sameOwner && e0.owner == 0)) worldedit::setOwner(doc, idx, 0);
        if (ImGui::Selectable("Враждебные монстры", sameOwner && e0.owner == -1)) worldedit::setOwner(doc, idx, -1);
        for (size_t p = 0; p < d.players.size(); ++p)
            if (ImGui::Selectable((d.players[p].name + "##p" + std::to_string(p)).c_str(), sameOwner && e0.owner == int(p + 1))) worldedit::setOwner(doc, idx, int(p + 1));
        ImGui::EndCombo();
    }
    ui::prop(idx.size() > 1 ? "Центр" : "Позиция", idx.size() > 1 ? "Перетаскивание сдвигает все выбранные объекты" : "x и z в клетках; перетаскивание в «Сцене» тоже двигает");
    if (!posActive_) { posEdit_[0] = cx; posEdit_[1] = cz; }
    const bool ch = ImGui::DragFloat2("##pos", posEdit_, 0.1f, 0, 0, "%.1f");
    if (ImGui::IsItemActivated()) doc.beginEdit();
    posActive_ = ImGui::IsItemActive();
    if (ch) worldedit::move(doc, idx, posEdit_[0] - cx, posEdit_[1] - cz);
    bool allGarrison = true, allNodes = true;
    for (int i : idx) {
        const int t = R.findType(d.entities[size_t(i)].type);
        allGarrison &= t >= 0 && R.types[size_t(t)].garrisonMax > 0;
        allNodes &= t >= 0 && R.types[size_t(t)].resourceNode >= 0;
    }
    if (allGarrison) {
        const int def = 0, cur = std::max(0, e0.garrison);
        ui::prop(sameGarrison && e0.garrison >= 0 ? "Гарнизон •" : "Гарнизон", "Юнитов внутри на старте. • — задано картой (по умолчанию 0)");
        int g = cur;
        const bool c2 = ImGui::SliderInt("##garrison", &g, 0, std::max(1, ti >= 0 ? R.types[size_t(ti)].garrisonMax * 2 : 20), sameGarrison ? "%d" : "—");
        if (ImGui::IsItemActivated()) doc.beginEdit();
        if (c2) { for (int i : idx) d.entities[size_t(i)].garrison = g; doc.touched(); }
        (void)def;
    }
    if (allNodes && ti >= 0) {
        const float def = float(R.types[size_t(ti)].resourceAmount.toDouble());
        char tip[128];
        std::snprintf(tip, sizeof tip, "-1 — бесконечный. • — задано картой (по умолчанию из типа: %.0f)", def);
        ui::prop(sameAmount && e0.amount >= 0 ? "Запас •" : "Запас", tip);
        float a = e0.amount >= 0 ? e0.amount : def;
        const bool c3 = ImGui::DragFloat("##amount", &a, 10.0f, -1.0f, 1e7f, sameAmount ? "%.0f" : "—");
        if (ImGui::IsItemActivated()) doc.beginEdit();
        if (c3) { for (int i : idx) d.entities[size_t(i)].amount = a; doc.touched(); }
    }
    ui::prop("Имя (id)", "Необязательно. Имя объекта для дорог, триггеров и скриптов");
    if (idx.size() == 1) {
        auto& id = d.entities[size_t(idx[0])].id;
        if (ImGui::InputTextWithHint("##id", "необязательно", id.data(), id.capacity() + 1, ImGuiInputTextFlags_CallbackResize, ui::stringResize, &id)) doc.touched();
    } else {
        ImGui::BeginDisabled();
        std::string dash = "—";
        ui::inputString("##id", dash);
        ImGui::EndDisabled();
    }
    if (idx.size() == 1 && e0.extra.isObject() && e0.extra.size() > 0) {
        ui::prop("Другие ключи", "Ключи объекта карты, которые редактор не разбирает (сохраняются как есть)");
        ImGui::TextWrapped("%s", e0.extra.dump(-1).c_str());
    }
    ui::endProps();
}

bool DetailsPanel::matches(const std::string& key, const Json& v) const {
    if (filter_.empty()) return true;
    if (ui::contains(key, filter_)) return true;
    if (v.isObject() && !isRef(v)) { for (auto& [k, x] : v.members()) if (matches(k, x)) return true; return false; }
    if (v.isArray()) { for (auto& x : v.items()) if (matches("", x)) return true; return false; }
    return ui::contains(scalar(v), filter_);
}

void DetailsPanel::rowMenu(const std::string& path, const Json& v) {
    if (!ImGui::BeginPopupContextItem("##prop")) return;
    if (ui::menuItem(ICON_COPY, "Копировать значение")) ImGui::SetClipboardText((v.isObject() || v.isArray()) && !isRef(v) ? v.dump().c_str() : scalar(v).c_str());
    if (ui::menuItem(ICON_LINK, "Копировать путь")) ImGui::SetClipboardText(path.c_str());
    if (ui::menuItem(ICON_BRACES, "Копировать как JSON")) ImGui::SetClipboardText(v.dump().c_str());
    ImGui::EndPopup();
}

void DetailsPanel::property(Editor& ed, const std::string& key, const Json& v, const std::string& path) {
    if (!matches(key, v)) return;
    ImGui::PushID(key.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool container = (v.isObject() && !isRef(v) && v.size() > 0) || (v.isArray() && !inlineArray(v));
    if (container) {
        const bool open = ImGui::TreeNodeEx(key.c_str(), ImGuiTreeNodeFlags_SpanAllColumns | (!filter_.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0));
        rowMenu(path, v);
        ImGui::TableNextColumn();
        ImGui::TextDisabled(v.isArray() ? "[%zu]" : "{%zu}", v.size());
        if (open) {
            if (v.isArray()) for (size_t i = 0; i < v.size(); ++i) property(ed, "[" + std::to_string(i) + "]", v[i], path + "[" + std::to_string(i) + "]");
            else for (auto& [k, x] : v.members()) property(ed, k, x, path + "." + k);
            ImGui::TreePop();
        }
    } else {
        ImGui::TreeNodeEx(key.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAllColumns);
        rowMenu(path, v);
        ImGui::TableNextColumn();
        if (isMixed(v)) {
            ImGui::TextDisabled("—");
            ImGui::SetItemTooltip("Значения различаются");
        } else if (isRef(v)) {   // another object: click selects it
            ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
            if (ImGui::Selectable((scalar(v) + "##ref").c_str())) {
                if (const WorldRow* r = ed.world().find(uint64_t(v["$entity"].integer()))) ed.select(ed.worldSelection(*r));
            }
            ImGui::PopStyleColor();
        } else {
            const std::string t = v.isArray() ? inlineText(v) : scalar(v);
            ImGui::TextUnformatted(t.c_str());
            if (ImGui::IsItemHovered() && ImGui::CalcTextSize(t.c_str()).x > ImGui::GetContentRegionAvail().x) ImGui::SetTooltip("%s", t.c_str());
        }
    }
    ImGui::PopID();
}

void DetailsPanel::section(Editor& ed, const Section& sec) {
    const char* id = sec.id.c_str();
    const char* title = sec.title.c_str();
    const Json& merged = sec.value;
    if (!filter_.empty() && !matches(title, merged) && !matches(id, merged)) return;
    char label[160];
    if (sec.have < withData_) std::snprintf(label, sizeof label, "%s  (%zu из %zu)###%s", title, sec.have, withData_, id);
    else std::snprintf(label, sizeof label, "%s###%s", title, id);
    if (!filter_.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!ImGui::CollapsingHeader(label, sec.open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    if (ImGui::BeginPopupContextItem()) {
        if (ui::menuItem(ICON_BRACES, "Копировать раздел как JSON")) ImGui::SetClipboardText(merged.dump().c_str());
        ImGui::EndPopup();
    }
    const std::string sectionFilter = filter_;
    if (!filter_.empty() && (ui::contains(title, filter_) || ui::contains(id, filter_))) filter_.clear();   // the section matched: all of it
    ImGui::PushID(id);
    if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        if (merged.isObject() && !isRef(merged)) for (auto& [k, x] : merged.members()) property(ed, k, x, sec.id + "." + k);
        else property(ed, "значение", merged, sec.id);
        ImGui::EndTable();
    }
    ImGui::PopID();
    filter_ = sectionFilter;
}

void DetailsPanel::sections(Editor& ed) {
    if (sections_.empty()) {
        const auto& P = ed.worldPreview();
        ImGui::Spacing();
        if (cursor_ < items_.size()) ui::hint(ICON_CLOCK "  Читаю данные объектов…");
        else if (!ed.worldLive() && !P.error().empty()) ui::callout(ui::Tone::Warn, "Данные мира недоступны: " + P.error());
        else if (!ed.worldLive() && (ed.worldPending() || !P.sim())) ui::hint(ICON_CLOCK "  Данные мира (компоненты, характеристики) появятся после обновления предпросмотра.");
        return;
    }
    if (truncated_) ui::hint(("Данные показаны для первых " + std::to_string(withData_) + " объектов").c_str());
    for (auto& sec : sections_) section(ed, sec);
}

} // namespace aiverse::editor
