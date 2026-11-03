// Inspector: properties of the current selection — map object, player, area, region, wave,
// trigger, map, or a rules item (type / resource / tech / faction) with block templates.
#include <algorithm>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace rts::editor {

namespace {
struct BlockTemplate { const char* key; const char* json; const char* hint; };
const BlockTemplate kTypeBlocks[] = {
    {"health", R"({"max": 100, "armor": 0, "armorType": "none"})", "Здоровье и броня"},
    {"move", R"({"speed": 2.5, "radius": 0.4, "layer": "ground"})", "Движение: ground / air / naval / sand"},
    {"weapons", R"([{"range": 1, "damage": 10, "cooldown": 1.0, "targets": ["ground"]}])", "Оружие"},
    {"cost", R"({"gold": 100, "time": 20, "supply": 1})", "Стоимость, время, лимит"},
    {"structure", R"({"footprint": [2, 2]})", "Здание: размер; on / takesOverNode / terrain"},
    {"produces", R"([])", "Что тренирует (здания здесь = стройка как в C&C)"},
    {"builds", R"([])", "Что строит рабочий"},
    {"researches", R"([])", "Какие техи исследует"},
    {"upgrades", R"([])", "Во что улучшается на месте (уровни)"},
    {"requires", R"([])", "Требования: типы или техи"},
    {"gather", R"({"gold": {"carry": 10, "time": 2}})", "Что добывает"},
    {"resourceNode", R"({"resource": "gold", "amount": 10000})", "Источник ресурса (amount < 0 — бесконечный)"},
    {"dropoff", R"(["gold"])", "Куда сдавать ресурсы"},
    {"supply", "10", "Даёт лимит"},
    {"income", R"({"gold": 1})", "Доход в секунду"},
    {"storage", R"({"gold": 500})", "Увеличивает хранилище"},
    {"provides", R"({"power": 100})", "Даёт мощность (энергия)"},
    {"consumes", R"({"power": 20})", "Потребляет мощность"},
    {"energy", R"({"max": 100, "regen": 0.5, "start": 50})", "Мана для способностей"},
    {"abilities", R"([{"id": "heal", "name": "Лечение", "target": "unit", "range": 6, "cooldown": 8, "energy": 25, "effects": [{"type": "heal", "amount": 50}]}])", "Способности"},
    {"hero", R"({"levels": [200, 500, 900], "perLevel": [{"stat": "maxHp", "add": 50}]})", "Герой: уровни и прирост"},
    {"squad", R"({"members": 4, "reinforceCost": {"gold": 25}, "reinforceTime": 4})", "Отряд (CoH)"},
    {"charges", R"({"max": 3, "interval": 11})", "Личинки: параллельное производство"},
    {"buildMode", R"("walk")", "walk / warp / morph"},
    {"creep", "6", "Распространяет слизь"},
    {"fieldSpawner", R"({"resource": "ore", "rate": 10, "radius": 3})", "Выращивает поле ресурса"},
    {"deploysTo", R"("yard")", "Разворачивается в здание (MCV)"},
    {"wreck", R"({"metal": 100})", "Оставляет обломки"},
    {"garrison", R"({"max": 20, "unit": "unit_id", "interval": 1.0})", "Гарнизон (Mushroom Wars)"},
    {"capture", R"({"time": 10, "radius": 3, "victoryPoint": false, "region": false})", "Точка захвата / флаг"},
    {"neutral", R"({"leash": 10, "wander": false})", "Поведение нейтралов"},
    {"waveSpawn", R"({"unit": "unit_id", "count": 2})", "Выпускает волну (lane battler)"},
    {"bounty", R"({"gold": 10})", "Награда за убийство"},
    {"tags", R"(["infantry"])", "Теги для техов и триггеров"},
    {"key", "true", "Ключевой объект (потеря = поражение)"},
    {"view", R"({"shape": "soldier", "scale": 1.0})", "Внешний вид"},
};
} // namespace

class InspectorWindow : public EditorWindow {
public:
    const char* id() const override { return "inspector"; }
    const char* title() const override { return "Инспектор"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "right"; }

    void draw(Editor& ed) override {
        Selection& s = ed.selection;
        if (!(s == lastSel_)) { message_.clear(); lastSel_ = s; }
        switch (s.kind) {
            case Selection::Entity: entity(ed); break;
            case Selection::Player: player(ed); break;
            case Selection::Area: area(ed); break;
            case Selection::Region: region(ed); break;
            case Selection::Wave: mapJson(ed, "waves[" + std::to_string(s.index) + "]", ICON_WAVES, "Волна " + std::to_string(s.index + 1), "Сценарная волна врагов"); break;
            case Selection::Trigger: mapJson(ed, "triggers[" + std::to_string(s.index) + "]", ICON_ZAP, "Триггер карты", "Событие → условия → действия"); break;
            case Selection::Type: rulesItem(ed, "types." + s.key, ICON_OBJECTS, "Тип объекта", true); break;
            case Selection::Resource: rulesItem(ed, "resources." + s.key, ICON_FIELDS, "Ресурс", false); break;
            case Selection::Tech: rulesItem(ed, "techs." + s.key, ICON_ZAP, "Технология", false); break;
            case Selection::Faction: rulesItem(ed, "factions." + s.key, ICON_SHIELD, "Фракция", false); break;
            case Selection::MapProps: mapProps(ed); break;
            default:
                ui::emptyStateHere(ICON_INSPECTOR, "Ничего не выбрано", "Выберите объект в «Сцене» или «Иерархии», элемент правил — в «Проекте».",
                                   ImGui::GetFontSize() * 9);
                const float bw = ImGui::CalcTextSize(ICON_REGIONS "  Свойства карты").x + ImGui::GetStyle().FramePadding.x * 2;
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - bw) * 0.5f));
                if (ui::button(ICON_REGIONS, "Свойства карты")) ed.select({Selection::MapProps, -1, {}});
        }
    }

private:
    static std::string ownerName(Editor& ed, int owner) {
        auto& d = ed.doc().data;
        return owner == 0 ? "Нейтральный" : owner < 0 ? "Враждебные монстры" : owner <= int(d.players.size()) ? d.players[size_t(owner - 1)].name : "?";
    }
    static ImVec4 ownerColor(int owner) {
        if (owner <= 0) return ImVec4(0, 0, 0, 0);   // accent
        const render::float4 c = game::RtsView::playerColor(owner);
        return ImVec4(c.x, c.y, c.z, 1);
    }

    // ---------------- map object
    void entity(Editor& ed) {
        auto& doc = ed.doc();
        auto& d = doc.data;
        int i = ed.selectedEntity();
        if (i < 0 || i >= int(d.entities.size())) { ed.select({}); return; }
        auto& e = d.entities[size_t(i)];
        const auto& rules = ed.rules();
        int ti = rules.findType(e.type);
        const bool structure = ti >= 0 && rules.types[size_t(ti)].structure;
        ui::headerCard(structure ? ICON_CASTLE : ICON_OBJECTS, ti >= 0 ? rules.types[size_t(ti)].name : e.type,
                       ownerName(ed, e.owner) + (e.id.empty() ? std::string() : "  ·  #" + e.id), ownerColor(e.owner));
        if (ui::beginProps("##entity")) {
            ui::prop("Тип");
            if (ImGui::BeginCombo("##type", ti >= 0 ? rules.types[size_t(ti)].name.c_str() : e.type.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (auto& t : rules.types) if (ImGui::Selectable((t.name + "##" + t.id).c_str(), t.id == e.type)) { doc.beginEdit(); d.entities[size_t(i)].type = t.id; }
                ImGui::EndCombo();
            }
            ui::prop("Владелец");
            ownerCombo(ed, "##owner", d.entities[size_t(i)].owner);
            ui::prop("Позиция", "x и z в клетках; перетаскивание в «Сцене» тоже двигает");
            float pos[2] = {e.x, e.z};
            bool ch = ImGui::DragFloat2("##pos", pos, 0.1f, 0, 0, "%.1f");
            if (ImGui::IsItemActivated()) doc.beginEdit();
            if (ch) { d.entities[size_t(i)].x = pos[0]; d.entities[size_t(i)].z = pos[1]; doc.touched(); }
            if (ti >= 0 && rules.types[size_t(ti)].garrisonMax > 0) {
                ui::prop("Гарнизон", "Юнитов внутри на старте");
                int g = std::max(0, d.entities[size_t(i)].garrison);
                bool c2 = ImGui::SliderInt("##garrison", &g, 0, rules.types[size_t(ti)].garrisonMax * 2);
                if (ImGui::IsItemActivated()) doc.beginEdit();
                if (c2) { d.entities[size_t(i)].garrison = g; doc.touched(); }
            }
            if (ti >= 0 && rules.types[size_t(ti)].resourceNode >= 0) {
                ui::prop("Запас", "-1 — бесконечный");
                float a = d.entities[size_t(i)].amount >= 0 ? d.entities[size_t(i)].amount : float(rules.types[size_t(ti)].resourceAmount.toDouble());
                bool c3 = ImGui::DragFloat("##amount", &a, 10.0f, -1.0f, 1e7f, "%.0f");
                if (ImGui::IsItemActivated()) doc.beginEdit();
                if (c3) { d.entities[size_t(i)].amount = a; doc.touched(); }
            }
            ui::prop("id", "Имя объекта для дорог и ссылок из триггеров");
            if (ImGui::InputTextWithHint("##id", "без имени", d.entities[size_t(i)].id.data(), d.entities[size_t(i)].id.capacity() + 1,
                                         ImGuiInputTextFlags_CallbackResize, ui::stringResize, &d.entities[size_t(i)].id))
                doc.touched();
            ui::endProps();
        }
        ImGui::Spacing();
        if (ui::wideButton(ICON_CROSSHAIR, "Показать", 0, 3)) ed.focusScene(e.x, e.z, 18);
        if (ui::wideButton(ICON_RULES, "Правила типа", 1, 3)) ed.select({Selection::Type, -1, e.type});
        if (ui::wideButton(ICON_TRASH, "Удалить", 2, 3)) { doc.removeEntity(i); ed.select({}); }
    }

    void ownerCombo(Editor& ed, const char* label, int& owner) {
        auto& d = ed.doc().data;
        if (ImGui::BeginCombo(label, ownerName(ed, owner).c_str())) {
            if (ImGui::Selectable("Нейтральный", owner == 0)) { ed.doc().beginEdit(); owner = 0; }
            if (ImGui::Selectable("Враждебные монстры", owner == -1)) { ed.doc().beginEdit(); owner = -1; }
            for (size_t p = 0; p < d.players.size(); ++p)
                if (ImGui::Selectable(d.players[p].name.c_str(), owner == int(p + 1))) { ed.doc().beginEdit(); owner = int(p + 1); }
            ImGui::EndCombo();
        }
    }

    // ---------------- player
    void player(Editor& ed) {
        auto& doc = ed.doc();
        auto& d = doc.data;
        int p = ed.selection.index;
        if (p < 0 || p >= int(d.players.size())) { ed.select({}); return; }
        auto& pl = d.players[size_t(p)];
        ui::headerCard(pl.waves ? ICON_WAVES : ICON_USER, pl.name, "Игрок " + std::to_string(p + 1) + "  ·  команда " + std::to_string(pl.team), ownerColor(p + 1));
        if (ui::beginProps("##player")) {
            ui::prop("Имя");
            if (ui::inputString("##name", pl.name)) doc.touched();
            ui::prop("Команда", "Игроки с одинаковой командой — союзники");
            if (ImGui::InputInt("##team", &pl.team)) doc.touched();
            if (!ed.rules().factions.empty()) {
                ui::prop("Фракция");
                if (ImGui::BeginCombo("##faction", pl.faction.empty() ? "(по умолчанию)" : pl.faction.c_str())) {
                    if (ImGui::Selectable("(по умолчанию)", pl.faction.empty())) { pl.faction.clear(); doc.touched(); }
                    for (auto& f : ed.rules().factions) if (ImGui::Selectable(f.name.c_str(), pl.faction == f.id)) { pl.faction = f.id; doc.touched(); }
                    ImGui::EndCombo();
                }
            }
            ui::prop("Сценарий", "Сторона без ИИ и без игрока: только волны и триггеры");
            if (ImGui::Checkbox("Волны вместо игрока##waves", &pl.waves)) doc.touched();
            ui::prop("Старт");
            if (pl.hasStart) {
                float st[2] = {pl.startX, pl.startZ};
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
                if (ImGui::DragFloat2("##start", st, 0.25f, 0, 0, "%.1f")) { pl.startX = st[0]; pl.startZ = st[1]; doc.touched(); }
                ImGui::SameLine(0, 4);
                if (ui::toolButton(ICON_X, "Убрать старт")) { pl.hasStart = false; doc.touched(); }
            } else if (ui::button(ICON_STARTS, "Задать на карте")) { ed.tool = Tool::Start; ed.startPlayer = p + 1; }
            ui::endProps();
        }
        bool anyRes = false;
        for (auto& rd : ed.rules().resources) anyRes |= rd.kind != game::ResourceKind::Capacity;
        if (anyRes) {
            ui::section(ICON_FIELDS, "Стартовые ресурсы");
            if (ui::beginProps("##res")) {
                for (auto& rd : ed.rules().resources) {
                    if (rd.kind == game::ResourceKind::Capacity) continue;
                    float v = float(rd.start.toDouble());
                    for (auto& [rid, val] : pl.resources) if (rid == rd.id) v = val;
                    ui::prop(rd.name.c_str());
                    if (ImGui::DragFloat(("##" + rd.id).c_str(), &v, 5.0f, 0.0f, 1e6f, "%.0f")) {
                        bool found = false;
                        for (auto& [rid, val] : pl.resources) if (rid == rd.id) { val = v; found = true; }
                        if (!found) pl.resources.push_back({rd.id, v});
                        doc.touched();
                    }
                }
                ui::endProps();
            }
        }
        ImGui::Spacing();
        const int buttons = (d.players.size() > 2 ? 1 : 0) + (d.players.size() < 16 ? 1 : 0);
        int k = 0;
        if (d.players.size() < 16 && ui::wideButton(ICON_PLUS, "Добавить игрока", k++, buttons)) {
            doc.beginEdit();
            static const char* names[] = {"Blue", "Red", "Green", "Yellow", "Purple", "Teal", "Orange", "Pink"};
            d.players.push_back({names[d.players.size() % 8], int(d.players.size() + 1)});
            ed.select({Selection::Player, int(d.players.size() - 1), {}});
            return;
        }
        if (d.players.size() > 2 && ui::wideButton(ICON_TRASH, "Удалить игрока", k++, buttons)) {
            Json prm = Json::object(); prm.set("player", Json::number(int64_t(p + 1)));
            ed.ws().call("player_remove", prm);
            ed.select({});
        }
    }

    // ---------------- area / region
    void area(Editor& ed) {
        auto& doc = ed.doc();
        Json* areas = doc.data.extra.find("areas");
        Json* a = areas ? areas->find(ed.selection.key) : nullptr;
        if (!a) { ed.select({}); return; }
        ui::headerCard(ICON_ZONES, ed.selection.key, "Зона для триггеров и волн");
        if (ui::beginProps("##area")) {
            ui::prop("Имя", "Enter — переименовать. Триггеры ссылаются на зону по имени.");
            std::string name = ed.selection.key;
            if (ui::inputString("##name", name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && !areas->has(name)) {
                doc.beginEdit();
                for (auto& [k, v] : areas->membersMut()) if (k == ed.selection.key) k = name;
                ed.selection.key = name;
                ui::endProps();
                return;
            }
            float r[4] = {float(a->at(0).toDouble()), float(a->at(1).toDouble()), float(a->at(2).toDouble()), float(a->at(3).toDouble())};
            ui::prop("Угол x, z");
            bool ch = ImGui::DragFloat2("##xy", r, 0.25f, 0, 0, "%.1f");
            ui::prop("Ширина, высота");
            ch |= ImGui::DragFloat2("##wh", r + 2, 0.25f, 0, 0, "%.1f");
            if (ch) {
                for (int k = 0; k < 4; ++k) a->at(size_t(k)) = Json::number(double(r[k]), 2);
                doc.touched();
            }
            ui::endProps();
        }
        ImGui::Spacing();
        const float cx = float(a->at(0).toDouble() + a->at(2).toDouble() / 2), cz = float(a->at(1).toDouble() + a->at(3).toDouble() / 2);
        if (ui::wideButton(ICON_CROSSHAIR, "Показать", 0, 2)) ed.focusScene(cx, cz, 18);
        if (ui::wideButton(ICON_TRASH, "Удалить зону", 1, 2)) { doc.beginEdit(); areas->erase(ed.selection.key); ed.select({}); }
    }

    void region(Editor& ed) {
        auto& d = ed.doc().data;
        char c = ed.selection.key.empty() ? '.' : ed.selection.key[0];
        int cells = int(std::count(d.regions.begin(), d.regions.end(), c));
        ui::headerCard(ICON_REGIONS, std::string("Регион ") + c, std::to_string(cells) + " клеток");
        if (ui::beginProps("##region")) {
            ui::prop("Название");
            std::string name = d.regionNames.count(c) ? d.regionNames[c] : "";
            if (ui::inputString("##name", name)) { d.regionNames[c] = name; ed.doc().touched(); }
            ui::endProps();
        }
        ImGui::Spacing();
        if (ui::wideButton(ICON_BRUSH, "Рисовать этот регион")) { ed.tool = Tool::Region; ed.regionLetter = c; }
    }

    // ---------------- JSON editing (map blocks and rules items)
    // Text field with live validation; returns true with the parsed value when «Применить» is pressed.
    bool jsonEditor(const std::string& key, const Json& current, Json& out, float minH = 220, bool withDelete = false, bool* deletePressed = nullptr) {
        std::string canon = current.dump();
        if (textFor_ != key + "\n" + canon) { text_ = canon; textFor_ = key + "\n" + canon; }
        const bool dirty = text_ != canon;
        std::string parseError;
        if (dirty) { try { (void)Json::parse(text_); } catch (const std::exception& e) { parseError = e.what(); } }
        float h = std::max(minH, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2 - 8);
        ui::inputMultiline("##json", text_, ImVec2(-1, h));
        // status line under the field
        if (!parseError.empty()) ui::toneText(ui::Tone::Error, "%s JSON: %s", ICON_ALERT, parseError.c_str());
        else if (dirty) ui::toneText(ui::Tone::Warn, "%s Есть неприменённые изменения", ICON_PENCIL);
        else ui::toneText(ui::Tone::Dim, "%s Совпадает с файлом", ICON_CHECK);
        const int n = withDelete ? 3 : 2;
        ImGui::BeginDisabled(!dirty || !parseError.empty());
        bool apply = ui::wideButton(ICON_CHECK, "Применить", 0, n, true);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!dirty);
        if (ui::wideButton(ICON_UNDO, "Отменить правку", 1, n)) textFor_.clear();
        ImGui::EndDisabled();
        if (withDelete && ui::wideButton(ICON_TRASH, "Удалить", 2, n) && deletePressed) *deletePressed = true;
        if (!apply) return false;
        try { out = Json::parse(text_); return true; }
        catch (const std::exception& e) { message_ = std::string("JSON: ") + e.what(); return false; }
    }

    void mapJson(Editor& ed, const std::string& path, const char* icon, const std::string& title, const std::string& subtitle) {
        ui::headerCard(icon, title, subtitle);
        const Json* cur = jsonPath(ed.doc().data.extra, path);
        if (!cur) { ed.select({}); return; }
        ui::resultMessage(message_);
        Json v;
        bool del = false;
        if (jsonEditor("map:" + path, *cur, v, 200, true, &del)) {
            Json prm = Json::object();
            prm.set("path", Json::string(path));
            prm.set("value", v);
            Json r = ed.ws().call("map_set", prm);
            message_ = r.has("error") ? "Ошибка: " + r.getStr("error") : "Применено";
            if (!r.has("error"))
                for (auto& is : ed.doc().validate(ed.rules(), ed.ws().project.rulesJson)) if (is.error) { message_ = "Ошибка: " + is.text; break; }
        }
        if (del) {
            ed.doc().beginEdit();
            std::string arr = path.substr(0, path.find('['));
            if (Json* a = ed.doc().data.extra.find(arr)) a->eraseAt(size_t(ed.selection.index));
            ed.select({});
            return;
        }
        ui::hint("Формат: docs/game-format.md (Maps, Triggers)");
    }

    void rulesItem(Editor& ed, const std::string& path, const char* icon, const char* kind, bool isType) {
        Json& R = ed.ws().project.rulesJson;
        const Json* cur = jsonPath(R, path);
        if (!cur) { ui::callout(ui::Tone::Warn, "Нет в правилах: " + path); return; }
        ui::headerCard(icon, cur->getStr("name", ed.selection.key), std::string(kind) + "  ·  " + path);
        ui::resultMessage(message_);
        if (isType && cur->has("move") && ui::beginProps("##body")) { bodyFields(ed, path, (*cur)["move"]); ui::endProps(); }
        if (isType) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##addblock", ICON_PLUS "  Добавить блок…", ImGuiComboFlags_HeightLarge)) {
                for (auto& b : kTypeBlocks) {
                    if (cur->has(b.key)) continue;
                    if (ImGui::Selectable((std::string(b.key) + "##" + b.key).c_str())) {
                        try { Json edited = Json::parse(text_); edited.set(b.key, Json::parse(b.json)); text_ = edited.dump(); }
                        catch (...) { message_ = "Сначала исправьте JSON"; }
                    }
                    ImGui::SameLine(ImGui::GetFontSize() * 8);
                    ImGui::TextDisabled("%s", b.hint);
                }
                ImGui::EndCombo();
            }
        }
        Json v;
        if (jsonEditor("rules:" + path, *cur, v)) {
            Json prm = Json::object();
            prm.set("path", Json::string(path));
            prm.set("value", v);
            Json r = ed.ws().call("rules_set", prm);
            message_ = r.has("error") ? "Ошибка: " + r.getStr("error") : "Сохранено в rules.json";
            if (!r.has("error")) ed.reloadRules();
        }
    }

    // Body of a mobile unit: radius (separation, ranges edge-to-edge, path clearance) and mass
    // (push priority, default 4·r²). Edited through rules_set like an agent would. Inside a props grid.
    void bodyFields(Editor& ed, const std::string& path, const Json& move) {
        float r = float(move.has("radius") ? move["radius"].toDouble() : 0.5);
        bool explicitMass = move.has("mass");
        float m = explicitMass ? float(move["mass"].toDouble()) : 4 * r * r;
        auto set = [&](const char* key, double v) {
            Json prm = Json::object();
            prm.set("path", Json::string(path + ".move." + key));
            prm.set("value", Json::number(v, 3));
            Json res = ed.ws().call("rules_set", prm);
            message_ = res.has("error") ? "Ошибка: " + res.getStr("error") : "Сохранено в rules.json";
            if (!res.has("error")) ed.reloadRules();
        };
        if (!bodyActive_) { radiusEdit_ = r; massEdit_ = m; } // follow the data unless being dragged
        bool active = false;
        ui::prop("Радиус тела", "move.radius: размер юнита — расталкивание (r1+r2), дальность атаки от края до края, проходы для больших.");
        ImGui::DragFloat("##radius", &radiusEdit_, 0.01f, 0.05f, 8.0f, "%.2f");
        active |= ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) set("radius", radiusEdit_);
        ui::prop(explicitMass ? "Масса" : "Масса (авто)", "move.mass: кто кого толкает (по умолчанию 4·r²). Атакующие и стоящие на позиции почти не сдвигаются.");
        ImGui::DragFloat("##mass", &massEdit_, 0.05f, 0.01f, 1000.0f, "%.2f");
        active |= ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) set("mass", massEdit_);
        bodyActive_ = active;
    }

    void mapProps(Editor& ed) {
        auto& doc = ed.doc();
        auto& d = doc.data;
        ui::headerCard(ICON_REGIONS, d.name.empty() ? std::string("Карта") : d.name,
                       std::to_string(d.w) + "×" + std::to_string(d.h) + " клеток  ·  объектов " + std::to_string(d.entities.size()) + "  ·  игроков " + std::to_string(d.players.size()));
        if (ui::beginProps("##map")) {
            ui::prop("Название");
            if (ui::inputString("##name", d.name)) doc.touched();
            ui::prop("Размер", "Ширина и высота в клетках; объекты за краем удаляются");
            if (sizeKey_ != d.w * 65536 + d.h) { sz_[0] = d.w; sz_[1] = d.h; sizeKey_ = d.w * 65536 + d.h; }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
            ImGui::InputInt2("##size", sz_);
            ImGui::SameLine(0, 4);
            ImGui::BeginDisabled(sz_[0] == d.w && sz_[1] == d.h);
            if (ui::toolButton(ICON_CHECK, "Изменить размер")) doc.resize(sz_[0], sz_[1]);
            ImGui::EndDisabled();
            ui::prop("Дороги", "Юниты ходят только по дорогам между зданиями (Mushroom Wars)");
            bool ro = d.roadsOnly;
            if (ImGui::Checkbox("Только по дорогам##ro", &ro)) { doc.beginEdit(); d.roadsOnly = ro; }
            ui::endProps();
        }
        ui::section(ICON_STARTS, "Победа (поверх правил)");
        Json v;
        if (jsonEditor("map:victory", d.extra["victory"], v, 60)) {
            doc.beginEdit();
            if (v.isNull()) d.extra.erase("victory"); else d.extra.set("victory", v);
        }
        ui::section(ICON_CIRCLE_CHECK, "Проверка");
        if (ui::wideButton(ICON_CIRCLE_CHECK, "Проверить карту", 0, 1, true)) { issues_ = doc.validate(ed.rules(), ed.ws().project.rulesJson); checked_ = true; }
        for (size_t i = 0; i < issues_.size(); ++i) {
            const auto& is = issues_[i];
            ImGui::PushID(int(i));
            ImGui::PushStyleColor(ImGuiCol_Text, ui::toneColor(is.error ? ui::Tone::Error : ui::Tone::Warn));
            if (ImGui::Selectable((std::string(is.error ? ICON_ALERT : ICON_WARNING) + "  " + is.text).c_str()) && is.entity >= 0 && is.entity < int(d.entities.size())) {
                ed.selectEntity(is.entity);
                ed.focusScene(d.entities[size_t(is.entity)].x, d.entities[size_t(is.entity)].z, 18);
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (checked_ && std::none_of(issues_.begin(), issues_.end(), [](const Issue& i) { return i.error; }))
            ui::callout(ui::Tone::Ok, issues_.empty() ? "Ошибок и предупреждений нет" : "Ошибок нет");
    }

    std::string text_, textFor_, message_;
    Selection lastSel_;
    float radiusEdit_ = 0, massEdit_ = 0;
    bool bodyActive_ = false, checked_ = false;
    int sz_[2] = {0, 0};
    int sizeKey_ = -1;
    std::vector<Issue> issues_;
};

RTS_EDITOR_WINDOW(InspectorWindow);

} // namespace rts::editor
