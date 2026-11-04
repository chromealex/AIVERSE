// Hierarchy: everything on the open map as a tree — players and their objects, neutral objects,
// creeps, areas, regions, waves, triggers. Click selects (Inspector), double-click focuses the
// Scene, right click: actions for the row (focus, delete, rules of the type) or add a wave / trigger.
#include <algorithm>
#include <map>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace rts::editor {

class HierarchyWindow : public EditorWindow {
public:
    const char* id() const override { return "hierarchy"; }
    const char* title() const override { return "Иерархия"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "left"; }

    void draw(Editor& ed) override {
        ui::searchField("##filter", filter_, "Объект, зона, триггер");
        ImGui::SameLine(0, 4);
        if (ui::toolButton(ICON_PLUS, "Добавить волну или триггер")) ImGui::OpenPopup("##add");
        if (ImGui::BeginPopup("##add")) { addMenu(ed); ImGui::EndPopup(); }
        auto& d = ed.doc().data;
        ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
        auto match = [&](const std::string& s) { return ui::contains(s, filter_); };
        auto leaf = [&](const std::string& label, Selection s, float fx = -1, float fz = -1) {
            bool sel = ed.selection == s;
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                ed.select(s);
                if (ImGui::IsMouseDoubleClicked(0) && fx >= 0) ed.focusScene(fx, fz, 18);
            }
            if (ImGui::BeginPopupContextItem()) {
                ed.select(s);
                if (fx >= 0 && ui::menuItem(ICON_CROSSHAIR, "Показать в «Сцене»")) ed.focusScene(fx, fz, 18);
                if (s.kind == Selection::Entity && s.index >= 0 && s.index < int(d.entities.size())) {
                    if (ui::menuItem(ICON_RULES, "Правила этого типа")) ed.select({Selection::Type, -1, d.entities[size_t(s.index)].type});
                    if (ui::menuItem(ICON_TRASH, "Удалить")) { ed.doc().removeEntity(s.index); ed.select({}); }
                }
                ImGui::EndPopup();
            }
        };
        auto node = [&](const char* icon, const std::string& text, size_t count, ImGuiTreeNodeFlags fl = 0) {
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s  %s", icon, text.c_str());
            const bool open = ImGui::TreeNodeEx(buf, fl | ImGuiTreeNodeFlags_SpanAvailWidth | (count == 0 ? ImGuiTreeNodeFlags_Leaf : 0));
            if (count > 0) { // count, dim, at the right edge of the row
                char c[32]; std::snprintf(c, sizeof c, "%zu", count);
                const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                const float w = ImGui::CalcTextSize(c).x;
                ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - w - 6, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), c);
            }
            return open;
        };
        const bool filtering = !filter_.empty();
        const ImGuiTreeNodeFlags openFl = ImGuiTreeNodeFlags_DefaultOpen;
        if (node(ICON_REGIONS, "Карта: " + d.name, d.entities.size(), openFl)) {
            if (ImGui::IsItemClicked()) ed.select({Selection::MapProps, -1, {}});
            std::map<int, std::map<std::string, std::vector<int>>> byOwner;
            for (size_t i = 0; i < d.entities.size(); ++i) byOwner[d.entities[i].owner][d.entities[i].type].push_back(int(i));
            auto ownedCount = [&](int owner) { size_t n = 0; for (auto& [t, v] : byOwner[owner]) n += v.size(); return n; };
            for (int owner = 1; owner <= int(d.players.size()); ++owner) {
                const auto& pl = d.players[size_t(owner - 1)];
                ImGui::PushID(owner);
                // a coloured dot of the player before the name
                const render::float4 pc = game::RtsView::playerColor(owner);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                bool nodeOpen = ImGui::TreeNodeEx("##pl", (filtering ? ImGuiTreeNodeFlags_DefaultOpen : 0) | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                              (byOwner[owner].empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                                                              (ed.selection.kind == Selection::Player && ed.selection.index == owner - 1 ? ImGuiTreeNodeFlags_Selected : 0),
                                                  "      %s%s", pl.name.c_str(), pl.waves ? "  (волны)" : "");
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ed.select({Selection::Player, owner - 1, {}});
                const float r = ImGui::GetTextLineHeight() * 0.28f;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cp.x + ImGui::GetTreeNodeToLabelSpacing() + r + 2, cp.y + ImGui::GetFrameHeight() * 0.5f - 1), r,
                                                            ImGui::GetColorU32(ImVec4(pc.x, pc.y, pc.z, 1)));
                if (size_t n = ownedCount(owner)) {
                    char c[32]; std::snprintf(c, sizeof c, "%zu", n);
                    const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ImGui::CalcTextSize(c).x - 6, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f),
                                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), c);
                }
                if (nodeOpen) { group(ed, byOwner[owner], match, leaf); ImGui::TreePop(); }
                ImGui::PopID();
            }
            for (int owner : {0, -1}) {
                if (byOwner[owner].empty()) continue;
                ImGui::PushID(owner + 100);
                if (node(owner == 0 ? ICON_TREES : ICON_SWORDS, owner == 0 ? "Нейтральные" : "Враждебные монстры", ownedCount(owner), filtering ? openFl : 0)) {
                    group(ed, byOwner[owner], match, leaf);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            const Json& areas = d.extra["areas"];
            if (areas.size() && node(ICON_ZONES, "Зоны", areas.size(), filtering ? openFl : 0)) {
                for (auto& [name, a] : areas.members())
                    if (match(name)) leaf(name, {Selection::Area, -1, name}, float(a[0].toDouble() + a[2].toDouble() / 2), float(a[1].toDouble() + a[3].toDouble() / 2));
                ImGui::TreePop();
            }
            std::vector<char> letters;
            for (char c : d.regions) if (c != '.' && std::find(letters.begin(), letters.end(), c) == letters.end()) letters.push_back(c);
            std::sort(letters.begin(), letters.end());
            if (!letters.empty() && node(ICON_REGIONS, "Регионы", letters.size(), filtering ? openFl : 0)) {
                for (char c : letters) {
                    std::string nm = std::string(1, c) + (d.regionNames.count(c) ? " — " + d.regionNames.at(c) : "");
                    if (match(nm)) leaf(nm, {Selection::Region, -1, std::string(1, c)});
                }
                ImGui::TreePop();
            }
            const Json& waves = d.extra["waves"];
            if (waves.size() && node(ICON_WAVES, "Волны", waves.size(), filtering ? openFl : 0)) {
                for (size_t i = 0; i < waves.size(); ++i) {
                    const Json& w = waves[i];
                    std::string nm = "Волна " + std::to_string(i + 1) + "  ·  " + std::to_string(w["time"].integer()) + " с";
                    if (match(nm)) leaf(nm, {Selection::Wave, int(i), {}}, float(w["at"][0].toDouble()), float(w["at"][1].toDouble()));
                }
                ImGui::TreePop();
            }
            const Json& trig = d.extra["triggers"];
            if (trig.size() && node(ICON_ZAP, "Триггеры карты", trig.size(), filtering ? openFl : 0)) {
                for (size_t i = 0; i < trig.size(); ++i) {
                    std::string nm = trig[i].getStr("id", "trigger" + std::to_string(i));
                    if (match(nm)) leaf(nm, {Selection::Trigger, int(i), {}});
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        if (d.entities.empty() && d.players.empty())
            ui::emptyStateHere(ICON_HIERARCHY, "Карта пуста", "Инструмент «Объекты» ставит юниты и здания.");
        if (ImGui::BeginPopupContextWindow("##ctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) { addMenu(ed); ImGui::EndPopup(); }
        ImGui::EndChild();
    }

private:
    void addMenu(Editor& ed) {
        const auto& rules = ed.rules();
        if (ui::menuItem(ICON_WAVES, "Добавить волну")) {
            ed.doc().beginEdit();
            Json w = Json::parse(R"({"time": 60, "player": 2, "at": [10, 10], "spawn": [], "every": 0})");
            if (!rules.types.empty()) { Json s = Json::object(); s.set("type", Json::string(rules.types[0].id)); s.set("count", Json::number(int64_t(5))); w.find("spawn")->push(s); }
            if (!ed.doc().data.extra["waves"].isArray()) ed.doc().data.extra.set("waves", Json::array());
            ed.doc().data.extra.find("waves")->push(w);
            ed.select({Selection::Wave, int(ed.doc().data.extra["waves"].size() - 1), {}});
        }
        if (ui::menuItem(ICON_ZAP, "Добавить триггер")) {
            ed.doc().beginEdit();
            Json t = Json::parse(R"({"id": "new_trigger", "event": {"type": "time", "seconds": 30}, "actions": [{"type": "message", "text": "Прошло 30 секунд"}]})");
            if (!ed.doc().data.extra["triggers"].isArray()) ed.doc().data.extra.set("triggers", Json::array());
            ed.doc().data.extra.find("triggers")->push(t);
            ed.select({Selection::Trigger, int(ed.doc().data.extra["triggers"].size() - 1), {}});
        }
        if (ui::menuItem(ICON_ZONES, "Нарисовать зону")) ed.tool = Tool::Area;
        if (ui::menuItem(ICON_OBJECTS, "Поставить объекты")) ed.tool = Tool::Place;
    }

    template <class Match, class Leaf>
    void group(Editor& ed, const std::map<std::string, std::vector<int>>& byType, Match&& match, Leaf&& leaf) {
        auto& d = ed.doc().data;
        for (auto& [type, idx] : byType) {
            int ti = ed.rules().findType(type);
            std::string tname = ti >= 0 ? ed.rules().types[size_t(ti)].name : type + " (?)";
            if (!match(tname) && !match(type)) continue;
            if (idx.size() == 1) {
                const auto& e = d.entities[size_t(idx[0])];
                leaf(tname + (e.id.empty() ? "" : "  #" + e.id) + "##" + std::to_string(idx[0]), {Selection::Entity, idx[0], {}}, e.x, e.z);
                continue;
            }
            const bool open = ImGui::TreeNodeEx((tname + "##" + type).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | (filter_.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen));
            {   // "×N" at the right edge of the row
                char c[32]; std::snprintf(c, sizeof c, "×%zu", idx.size());
                const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ImGui::CalcTextSize(c).x - 6, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f),
                                                    ImGui::GetColorU32(ImGuiCol_TextDisabled), c);
            }
            if (open) {
                for (int i : idx) {
                    const auto& e = d.entities[size_t(i)];
                    char buf[128];
                    std::snprintf(buf, sizeof buf, "%s  (%.0f, %.0f)##%d", e.id.empty() ? tname.c_str() : e.id.c_str(), e.x, e.z, i);
                    leaf(buf, {Selection::Entity, i, {}}, e.x, e.z);
                }
                ImGui::TreePop();
            }
        }
    }
    std::string filter_;
};

RTS_EDITOR_WINDOW(HierarchyWindow);

} // namespace rts::editor
