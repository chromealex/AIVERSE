// Inspector «Детали»: see DetailsPanel.h.
#include "DetailsPanel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include "EditorUtil.h"
#include "EditorWorldEdit.h"
#include "EditorComponentPanel.h"
#include "EditorEcsWriteback.h"
#include "EditorSelectionAdapter.h"
#include "EditorRuntimeEntityProvider.h"

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
    // Render the components actually reported by the selected entities, not just
    // the RTS registry. Native/FPS/project-specific components can have arbitrary IDs.
    // The registry supplies friendly titles and ordering where known.
    static const char* const open[] = {"Transform", "Owner", "Unit", "Orders", "Production", "Construction", "Garrison", "Cargo", "ResourceStock", "ScriptData", "Boarded", "Transport"};
    std::vector<std::string> componentIds;
    for (auto* it : withData) {
        const Json& components = it->data["components"];
        if (!components.isObject()) continue;
        for (auto& [id, value] : components.members()) {
            if (std::find(componentIds.begin(), componentIds.end(), id) == componentIds.end()) componentIds.push_back(id);
        }
    }
    const auto& registry = worldComponents();
    auto componentRank = [&](const std::string& id) {
        if (id == "Transform") return -1;
        for (size_t i = 0; i < registry.size(); ++i) if (id == registry[i].id) return int(i);
        return int(registry.size());
    };
    std::stable_sort(componentIds.begin(), componentIds.end(), [&](const std::string& a, const std::string& b) {
        return componentRank(a) < componentRank(b);
    });
    for (const auto& id : componentIds) {
        std::vector<const Json*> values;
        for (auto* it : withData) {
            const Json& components = it->data["components"];
            if (components.isObject() && components.has(id)) values.push_back(&components[id]);
        }
        if (values.empty()) continue;
        std::string title = worldComponentDisplayName(id);
        for (auto* it : withData) {
            const Json& descriptor = it->data["componentSchemas"][id];
            if (descriptor.isObject() && descriptor["title"].isString()) {
                title = descriptor["title"].str();
                break;
            }
        }
        bool expanded = id == "Transform";
        for (auto* o : open) if (id == o) { expanded = true; break; }
        sections_.push_back({id, title, merge(values), values.size(), expanded});
    }
    // the rules block of the type
    std::vector<const Json*> rules;
    for (auto* it : withData) if (it->data["rules"].isObject()) rules.push_back(&it->data["rules"]);
    if (!rules.empty()) sections_.push_back({"rules", "Правила типа (rules.json)", merge(rules), rules.size(), false});
}

void DetailsPanel::draw(Editor& ed) {
    if (ed.selection.kind == Selection::FpsActor ||
        ed.selection.kind == Selection::FpsRuntimeObject) {
        const auto runtime = enumerateEditorRuntimeEntities(ed);
        const auto found = std::find_if(runtime.begin(), runtime.end(),
            [&](const EditorRuntimeEntity& item) {
                return item.selection == ed.selection;
            });
        if (found == runtime.end()) {
            ui::hint("FPS ECS runtime entity is unavailable; rebuild the preview.");
            return;
        }
        const Json& actor = found->fields;
        ImGui::TextDisabled("FPS ECS ENTITY · READ ONLY");
        ImGui::TextUnformatted(found->title.c_str());
        const Json& components = actor["components"];
        if (components.isObject()) {
            for (const auto& [componentId, values] : components.members()) {
                if (!filter_.empty() &&
                    !matches(componentId, values)) continue;
                const bool expanded = ImGui::CollapsingHeader(
                    componentId.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                if (!expanded || !values.isObject()) continue;
                ImGui::PushID(componentId.c_str());
                if (ImGui::BeginTable("##fpsEcsFields", 2,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_RowBg)) {
                    for (const auto& [field, value] : values.members())
                        property(ed, field, value, componentId + "." + field);
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
        } else {
            ui::hint("No ECS components are present for this FPS actor.");
        }
        ImGui::TextDisabled("Preview ECS components; gameplay state remains "
                            "backward-compatible with FPS save v9.");
        return;
    }
    // FPS authored objects are document sources; they do not have ECS snapshot
    // rows yet. They nevertheless use the same Inspector panel and registered
    // document-component card drawers as any other authored entity.
    if (ed.selection.kind == Selection::FpsObject) {
        DocumentEntityRef ref;
        if (!documentEntityFromSelection(ed.selection, ref)) {
            ui::hint("Invalid scene object reference");
            return;
        }
        const Json* source = fpsDocumentEntity(ed.doc().data.fps, ref);
        if (!source) {
            ui::hint("Scene object no longer exists");
            return;
        }
        const bool collider = ref.source == DocumentEntityRef::Source::FpsCollider;
        ImGui::TextDisabled("SCENE OBJECT");
        ImGui::Text("%s #%zu", collider ? "Box Collider" : "Spawn Point", ref.index + 1);
        ImGui::Separator();
        drawDocumentEditableComponents(ed.doc(), ref);
        if (ImGui::CollapsingHeader("Raw data")) {
            ImGui::TextWrapped("%s", source->dump().c_str());
        }
        return;
    }
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
    // Runtime ECS component snapshots are authoritative for component cards.
    // Authoring metadata is deliberately separate and follows those cards.
    sections(ed);
    if (!ed.worldLive() && items_.size() == 1 && items_[0].doc >= 0) {
        ImGui::PushID("MapEntityMetadata");
        if (ImGui::CollapsingHeader("Map Entity Metadata"))
            mapFields(ed);
        ImGui::PopID();
    } else {
        mapFields(ed);
    }
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
            const auto& authored = d.entities[size_t(first.doc)];
            const std::string displayName = authored.extra.getStr("displayName", "");
            if (!displayName.empty()) title = displayName;
            if (!authored.id.empty()) title += "  #" + authored.id;
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
    if (idx.size() > 1) ui::section(ICON_PENCIL, "Map Entity Metadata (все выбранные)");
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
    // Single-object Position XZ is edited by the shared component drawer
    // above. Preserve the centroid editor for multi-selection only.
    if (idx.size() > 1) {
        ui::prop("Центр", "Перетаскивание сдвигает все выбранные объекты");
        if (!posActive_) { posEdit_[0] = cx; posEdit_[1] = cz; }
        const bool ch = ImGui::DragFloat2("##pos", posEdit_, 0.1f, 0, 0, "%.1f");
        if (ImGui::IsItemActivated()) doc.beginEdit();
        posActive_ = ImGui::IsItemActive();
        if (ch) {
            std::vector<DocumentEntityRef> refs;
            refs.reserve(idx.size());
            for (int i : idx) refs.push_back(DocumentEntityRef::mapEntity(size_t(i)));
            translateDocumentEntities(doc, refs, {posEdit_[0] - cx, 0.0, posEdit_[1] - cz});
        }
    } else {
        posActive_ = false;
    }
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
    if (idx.size() == 1) {
        ui::prop("Название", "Отображаемое имя объекта в редакторе; не меняет идентификатор для скриптов");
        auto& authored = d.entities[size_t(idx[0])];
        std::string displayName = authored.extra.getStr("displayName", "");
        const bool nameChanged = ui::inputString("##displayName", displayName);
        if (ImGui::IsItemActivated()) textEditSnapshot_ = false;
        if (nameChanged) {
            // one snapshot per edit session, not per keystroke
            if (!textEditSnapshot_) { doc.beginEdit(); textEditSnapshot_ = true; }
            authored.extra.set("displayName", Json::string(displayName));
            doc.touched();
        }
    }
    ui::prop("Имя (id)", "Необязательно. Имя объекта для дорог, триггеров и скриптов");
    if (idx.size() == 1) {
        auto& id = d.entities[size_t(idx[0])].id;
        // Snapshot before the first change of the session: the edit reaches Undo.
        // InputText writes into `id` itself, so snapshot from a copy taken before it.
        std::string before = id;
        const bool idChanged = ImGui::InputTextWithHint("##id", "необязательно", id.data(), id.capacity() + 1, ImGuiInputTextFlags_CallbackResize, ui::stringResize, &id);
        if (ImGui::IsItemActivated()) textEditSnapshot_ = false;
        if (idChanged) {
            if (!textEditSnapshot_) {
                std::string after = std::move(id);
                id = std::move(before);
                doc.beginEdit();
                d.entities[size_t(idx[0])].id = std::move(after);
                textEditSnapshot_ = true;
            }
            doc.touched();
        }
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
    // Typed metadata is registered alongside the real ECS component dump.
    // Unknown native/project fields remain visible through the generic JSON drawer.
    std::optional<WorldComponentField> fieldSchema;
    if (const auto dot = path.find('.'); dot != std::string::npos &&
        path.find('.', dot + 1) == std::string::npos) {
        for (const auto& field : worldComponentFields(path.substr(0, dot)))
            if (path.substr(dot + 1) == field.id) {
                fieldSchema = field;
                break;
            }
    }
    // Native module schemas travel with the real component snapshot. Never
    // assume that a similarly named component in another project shares it.
    std::string schemaTitle;
    bool nativeEditable = false;
    std::string nativeComponent, nativeField;
    std::string nativeKind;
    if (const auto dot = path.find('.'); dot != std::string::npos &&
        path.find('.', dot + 1) == std::string::npos) {
        for (const auto& item : items_) {
            const Json& meta = item.data["componentSchemas"][path.substr(0, dot)]
                                        ["fields"][path.substr(dot + 1)];
            if (meta.isObject()) {
                if (meta["title"].isString()) schemaTitle = meta["title"].str();
                nativeEditable = meta.getBool("editable", false);
                nativeKind = meta.getStr("kind");
                nativeComponent = path.substr(0, dot);
                nativeField = path.substr(dot + 1);
                break;
            }
        }
    }
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
        ImGui::TreeNodeEx(!schemaTitle.empty() ? schemaTitle.c_str() :
            fieldSchema ? fieldSchema->title.c_str() : key.c_str(),
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAllColumns);
        rowMenu(path, v);
        ImGui::TableNextColumn();
        bool customHandled = false;
        if (!ed.worldLive() && items_.size() == 1 && items_[0].doc >= 0) {
            auto& drawers = InspectorDrawerRegistry::instance();
            const auto custom = drawers.fields.find(path);
            if (custom != drawers.fields.end())
                customHandled = custom->second(ed.doc(),
                    DocumentEntityRef::mapEntity(size_t(items_[0].doc)),
                    key.c_str(), v);
        }
        if (customHandled) {
            // The registered field drawer rendered its own value controls.
        } else if (isMixed(v)) {
            ImGui::TextDisabled("—");
            ImGui::SetItemTooltip("Значения различаются");
        } else if (isRef(v)) {   // another object: click selects it
            ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
            if (ImGui::Selectable((scalar(v) + "##ref").c_str())) {
                if (const WorldRow* r = ed.world().find(uint64_t(v["$entity"].integer()))) ed.select(ed.worldSelection(*r));
            }
            ImGui::PopStyleColor();
        } else if (nativeEditable && !ed.worldLive() && items_.size() == 1 &&
                   items_[0].doc >= 0 && ed.worldModule() &&
                   (v.isNumber() || v.isString() ||
                    v.type() == Json::Type::Bool ||
                    (v.isArray() && v.size() == 3))) {
            // The owning native module explicitly declared this field
            // editable and will validate its authoring JSON writeback. No
            // arbitrary mutation of transient World/Simulation snapshots.
            Json updated;
            bool changed = false;
            if (v.type() == Json::Type::Bool) {
                bool value = v.boolean();
                changed = ImGui::Checkbox("##nativeBool", &value);
                if (changed) updated = Json::fromBool(value);
            } else if (v.isString()) {
                std::string value = v.str();
                changed = ui::inputString("##nativeString", value);
                if (changed) updated = Json::string(value);
            } else if (v.isArray() && v.size() == 3 &&
                       v[0].isNumber() && v[1].isNumber() && v[2].isNumber()) {
                float xyz[3] = {float(v[0].toDouble()), float(v[1].toDouble()),
                                float(v[2].toDouble())};
                ImGui::SetNextItemWidth(-1);
                changed = ImGui::DragFloat3("##nativeVec3", xyz, 0.05f);
                if (changed) {
                    updated = Json::array();
                    for (float n : xyz) updated.push(Json::number(double(n), 3));
                }
            } else if (v.isNumber() && nativeKind == "integer") {
                int n = int(v.integer());
                ImGui::SetNextItemWidth(-1);
                changed = ImGui::DragInt("##nativeInt", &n, 1.0f);
                if (changed) updated = Json::number(int64_t(n));
            } else if (v.isNumber()) {
                float n = float(v.toDouble());
                ImGui::SetNextItemWidth(-1);
                changed = ImGui::DragFloat("##nativeNumber", &n, 0.05f);
                if (changed) updated = Json::number(double(n), 3);
            }
            if (ImGui::IsItemActivated()) ed.doc().beginEdit();
            if (changed) {
                const auto ref = DocumentEntityRef::mapEntity(size_t(items_[0].doc));
                writeNativeAuthoredEcsField(ed.doc(), ed.worldModule()->native(),
                                            ref, nativeComponent, nativeField,
                                            updated);
            }
        } else if (path == "Owner.player" && v.isNumber()) {
            const bool authored = !ed.worldLive() && items_.size() == 1 &&
                                  items_[0].doc >= 0;
            const auto ref = DocumentEntityRef::mapEntity(
                authored ? size_t(items_[0].doc) : 0);
            const bool editable = authored && canWriteAuthoredEcsField(
                ed.doc(), ref, "Owner", "player");
            // Show the ECS value, but edit the serialized map value so the
            // preview can be rebuilt and undo/redo remains deterministic.
            const DocEntity* source = editable ? documentMapEntity(ed.doc(), ref) : nullptr;
            int player = source ? source->owner : int(v.integer());
            const int maxPlayer = int(std::min<size_t>(
                ed.doc().data.players.size(), 255));
            ImGui::BeginDisabled(!editable || player < 0);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##ecsOwner", &player, 0, maxPlayer)) {
                writeAuthoredEcsField(ed.doc(), ref, "Owner", "player",
                                      Json::number(int64_t(player)));
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Persisted owner of a map-authored ECS entity; live entities are read-only.");
        } else if (path == "Unit.facing" && v.isArray() && v.size() == 3 &&
                   !ed.worldLive() && items_.size() == 1 && items_[0].doc >= 0 &&
                   size_t(items_[0].doc) < ed.doc().data.entities.size() &&
                   ed.doc().data.entities[size_t(items_[0].doc)].extra["facing"].isNumber()) {
            // Unit.facing is the real ECS direction vector, produced at
            // Simulation::setup from the serialized authoring facing degrees.
            // Edit that persisted input, not a transient World snapshot.
            const auto ref = DocumentEntityRef::mapEntity(size_t(items_[0].doc));
            float degrees = float(ed.doc().data.entities[size_t(items_[0].doc)]
                                  .extra["facing"].toDouble());
            ImGui::SetNextItemWidth(-1);
            const bool changed = ImGui::DragFloat("##ecsFacing", &degrees,
                                                   0.5f, 0, 0, "%.2f deg");
            if (ImGui::IsItemActivated()) ed.doc().beginEdit();
            if (changed) {
                Json edited = Json::object();
                edited.set("rotationY", Json::number(double(degrees), 3));
                writeDocumentComponent(ed.doc(), ref,
                                       DocumentComponentKind::Transform, edited);
            }
        } else if (fieldSchema && fieldSchema->kind == WorldFieldKind::Turn && v.isNumber()) {
            ImGui::Text("%.2f°", v.toDouble() * 360.0);
            ImGui::SetItemTooltip("ECS rotation is stored in turns; no persistent map rotation field exists.");
        } else if (v.type() == Json::Type::Bool) {
            // Read-only ECS snapshots should look like typed properties rather than JSON.
            bool checked = v.boolean();
            ImGui::BeginDisabled();
            ImGui::Checkbox("##value", &checked);
            ImGui::EndDisabled();
        } else if (path == "Transform.pos" && v.isArray() && v.size() == 3 &&
                   v[0].isNumber() && v[1].isNumber() && v[2].isNumber()) {
            // This is the real ECS Transform snapshot. For map-authored RTS
            // entities only X/Z have a persistent document representation;
            // world-space Y is visible but intentionally not writable.
            const bool authored = !ed.worldLive() && items_.size() == 1 &&
                                  items_[0].doc >= 0;
            const float values[3] = {float(v[0].toDouble()),
                                     float(v[1].toDouble()),
                                     float(v[2].toDouble())};
            DocumentTransform docPosition;
            const DocumentEntityRef ref = DocumentEntityRef::mapEntity(
                authored ? size_t(items_[0].doc) : 0);
            const bool editable = authored && readDocumentTransform(
                ed.doc(), ref, docPosition);
            float x = editable ? float(docPosition.x) : values[0];
            float y = values[1];
            float z = editable ? float(docPosition.z) : values[2];
            const float width = std::max(55.0f,
                (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f);
            bool changed = false;
            ImGui::BeginDisabled(!editable);
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("X##ecsPos", &x, 0.05f, 0, 0, "%.3f");
            if (ImGui::IsItemActivated()) ed.doc().beginEdit();
            ImGui::EndDisabled();
            ImGui::SameLine(0, 4);
            ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(width);
            ImGui::DragFloat("Y##ecsPos", &y, 0, 0, 0, "%.3f");
            ImGui::EndDisabled();
            ImGui::SameLine(0, 4);
            ImGui::BeginDisabled(!editable);
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("Z##ecsPos", &z, 0.05f, 0, 0, "%.3f");
            if (ImGui::IsItemActivated()) ed.doc().beginEdit();
            ImGui::EndDisabled();
            if (changed)
                writeAuthoredEcsField(ed.doc(), ref, "Transform", "pos",
                    [&] {
                        Json values = Json::array();
                        values.push(Json::number(double(x), 3));
                        values.push(Json::number(double(y), 3));
                        values.push(Json::number(double(z), 3));
                        return values;
                    }());
            if (editable && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Y comes from the ECS world and is not serialized by this map.");
        } else if (v.isArray() && v.size() == 3 &&
                   v[0].isNumber() && v[1].isNumber() && v[2].isNumber()) {
            // A three-component vector gets the same compact XYZ layout for
            // positions, velocities and arbitrary native ECS vector fields.
            const char* axes[] = {"X", "Y", "Z"};
            const ImVec4 colors[] = {ImVec4(.95f,.37f,.36f,1), ImVec4(.39f,.82f,.50f,1), ImVec4(.37f,.61f,.96f,1)};
            const float width = ImGui::GetContentRegionAvail().x / 3.0f;
            for (int i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine(0, 3);
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
                ImGui::TextUnformatted(axes[i]);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 3);
                ImGui::SetNextItemWidth(std::max(25.0f, width - 22.0f));
                ImGui::BeginDisabled();
                float value = float(v[size_t(i)].toDouble());
                ImGui::DragFloat((std::string("##axis") + axes[i]).c_str(), &value, 0.0f, 0.0f, 0.0f, "%.3f");
                ImGui::EndDisabled();
                ImGui::EndGroup();
            }
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
    // Studio component card: subtle raised background and a generous hit area.
    // Keep the original ImGui header ID so saved collapse state is preserved.
    const auto& studio = EditorTheme::current();
    ImGui::PushStyleColor(ImGuiCol_Header, studio.panelRaised);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, studio.frameHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, studio.frameHover);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 9));
    const bool expanded = ImGui::CollapsingHeader(label, sec.open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    const ImVec2 headerMin = ImGui::GetItemRectMin();
    const ImVec2 headerMax = ImGui::GetItemRectMax();
    // A thin accent rail identifies the Transform card without filling the UI
    // with saturated backgrounds. Other components use a muted divider.
    const ImVec4 accent = sec.id == "Transform" ? ui::accentColor() : studio.frameHover;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(headerMin.x, headerMin.y + 3.0f),
        ImVec2(headerMin.x + 3.0f, headerMax.y - 3.0f),
        ImGui::GetColorU32(accent), 1.5f);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s  |  %zu/%zu entities", id, sec.have, withData_);
    if (!expanded) { ImGui::Dummy(ImVec2(0, 5)); return; }
    if (ImGui::BeginPopupContextItem()) {
        if (ui::menuItem(ICON_BRACES, "Копировать раздел как JSON")) ImGui::SetClipboardText(merged.dump().c_str());
        ImGui::EndPopup();
    }
    const std::string sectionFilter = filter_;
    if (!filter_.empty() && (ui::contains(title, filter_) || ui::contains(id, filter_))) filter_.clear();   // the section matched: all of it
    ImGui::PushID(id);
    ImGui::Indent(8.0f);
    // C++ custom component drawers apply to actual ECS component snapshots
    // as well as authored document components. The enclosing ECS card and
    // component ID always come from WorldInspect, never DocEntity.
    if (!ed.worldLive() && items_.size() == 1 && items_[0].doc >= 0 &&
        sec.id != "stats" && sec.id != "rules") {
        auto& drawers = InspectorDrawerRegistry::instance();
        auto custom = drawers.components.find(sec.id);
        if (custom != drawers.components.end()) {
            custom->second(ed.doc(),
                DocumentEntityRef::mapEntity(size_t(items_[0].doc)), merged);
            ImGui::Unindent(8.0f);
            ImGui::PopID();
            ImGui::Spacing();
            filter_ = sectionFilter;
            return;
        }
    }
    if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        if (merged.isObject() && !isRef(merged)) for (auto& [k, x] : merged.members()) property(ed, k, x, sec.id + "." + k);
        else property(ed, "значение", merged, sec.id);
        ImGui::EndTable();
    }
    ImGui::Unindent(8.0f);
    ImGui::PopID();
    ImGui::Spacing();
    filter_ = sectionFilter;
}

void DetailsPanel::sections(Editor& ed) {
    if (sections_.empty()) {
        const auto& P = ed.worldPreview();
        ImGui::Spacing();
        if (cursor_ < items_.size()) {
            ui::hint(ICON_CLOCK "  Читаю компоненты ECS…");
        } else if (!ed.worldLive() && !P.error().empty()) {
            ui::callout(ui::Tone::Warn,
                "Компоненты ECS недоступны: " + P.error() +
                ". Проверьте сборку редактора с native-модулями проекта.");
        } else if (!ed.worldLive() && (ed.worldPending() || !ed.worldFresh() || !P.sim())) {
            ui::hint(ICON_CLOCK "  Ожидание ECS World: компоненты появятся после обновления предпросмотра.");
        } else if (withData_ == 0) {
            // A map-authored object is NOT itself an ECS entity. If no runtime
            // row exists, showing synthetic document components is misleading.
            ui::callout(ui::Tone::Warn,
                "Для выбранного объекта не найдена сущность в ECS World. "
                "Компоненты не подменяются полями карты. Проверьте создание "
                "объекта в модуле и соответствие map→ECS.");
        } else {
            ui::hint(ICON_CLOCK "  ECS не вернул компонентов для выбранной сущности.");
        }
        if (!ed.worldLive()) {
            if (ImGui::SmallButton("Обновить ECS предпросмотр"))
                ed.refreshWorld();
        }
        return;
    }
    if (truncated_) ui::hint(("Данные показаны для первых " + std::to_string(withData_) + " объектов").c_str());
    for (auto& sec : sections_) section(ed, sec);
}

} // namespace aiverse::editor
