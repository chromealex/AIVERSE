// Project: the open game — maps and rules items (types, resources, techs, factions). Categories on
// the left, the items of the chosen one on the right as a wrapping grid; click selects (Inspector),
// double click on a map opens it, «+» creates a rules item through rules_set.
#include <algorithm>
#include <filesystem>
#include "../Editor.h"
#include "../EditorGenreUi.h"
#include "../EditorUtil.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

class ProjectWindow : public EditorWindow {
public:
    const char* id() const override { return "project"; }
    const char* title() const override { return "Проект"; }
    const char* group() const override { return "logic"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        if(ed.ws().project.isFps()) {
            ImGui::TextWrapped("%s",genreText(ed.ws(),"fps.editor.hint").c_str());
            if(ImGui::Button(genreText(ed.ws(),"fps.editor.open").c_str()))ed.focusWindow("fps");
            return;
        }
        auto& ws = ed.ws();
        if (ws.project.dir.empty()) {
            ui::emptyStateHere(ICON_PROJECT, "Игра не открыта", "Файл → Открыть проект");
            return;
        }
        // header: game name, search, other project
        ImGui::PushFont(ui::headingFont(), 0.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ws.project.manifest.getStr("name", "без названия").c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        const float btnW = ImGui::CalcTextSize(ICON_FOLDER_OPEN "  Другой проект…").x + ImGui::GetStyle().FramePadding.x * 2;
        const float searchW = std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.35f);
        const float rightX = ImGui::GetWindowContentRegionMax().x - searchW - btnW - ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::AlignTextToFramePadding();
        ui::pathText(ws.project.dir, rightX - ImGui::GetCursorPosX() - 12);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, rightX));
        ui::searchField("##f", filter_, "Фильтр", searchW);
        ImGui::SameLine();
        if (ui::button(ICON_FOLDER_OPEN, "Другой проект…")) ed.showGamePicker();
        if (!ws.project.rulesError.empty()) ui::callout(ui::Tone::Error, "Ошибка в правилах: " + ws.project.rulesError);

        static const struct { const char* key; const char* name; const char* icon; Selection::Kind kind; } cats[] = {
            {"maps", "Карты", ICON_MAP_PIN, Selection::None}, {"types", "Типы", ICON_OBJECTS, Selection::Type},
            {"resources", "Ресурсы", ICON_FIELDS, Selection::Resource}, {"techs", "Технологии", ICON_ZAP, Selection::Tech},
            {"factions", "Фракции", ICON_SHIELD, Selection::Faction}};
        std::vector<std::string> maps;
        {
            std::error_code ec;
            for (auto& e : fs::directory_iterator(ws.mapsDir(), ec)) if (e.path().extension() == ".json") maps.push_back(e.path().filename().string());
            std::sort(maps.begin(), maps.end());
        }
        const Json& R = ws.project.rulesJson;
        if (!ImGui::BeginTable("##proj", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, -FLT_MIN))) return;
        ImGui::TableSetupColumn("cats", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10);
        ImGui::TableSetupColumn("items", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        for (int i = 0; i < 5; ++i) {
            const size_t n = i == 0 ? maps.size() : R[cats[i].key].size();
            char label[96];
            std::snprintf(label, sizeof label, "%s  %s", cats[i].icon, cats[i].name);
            if (ImGui::Selectable(label, cat_ == i, 0, ImVec2(0, ImGui::GetFrameHeight()))) cat_ = i;
            char c[32]; std::snprintf(c, sizeof c, "%zu", n);
            const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ImGui::CalcTextSize(c).x - 4, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), c);
        }
        ImGui::TableNextColumn();
        ImGui::BeginChild("##items", ImVec2(0, 0));
        if (cat_ == 0) mapItems(ed, maps);
        else rulesItems(ed, cats[cat_].key, cats[cat_].kind, cats[cat_].icon);
        ImGui::EndChild();
        ImGui::EndTable();
    }

private:
    // Wrapping grid of equal-width cells; call cell() before each item.
    struct Grid {
        float w = 0; int cols = 1, n = 0;
        explicit Grid(float minW) {
            const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
            cols = std::max(1, int((avail + gap) / (minW + gap)));
            w = (avail - gap * float(cols - 1)) / float(cols);
        }
        void cell() { if (n++ % cols) ImGui::SameLine(); }
    };

    void mapItems(Editor& ed, const std::vector<std::string>& maps) {
        auto& ws = ed.ws();
        ui::hint("Двойной клик — открыть карту. Новая карта: Файл → Новая карта.");
        Grid g(ImGui::GetFontSize() * 13);
        for (auto& m : maps) {
            if (!ui::contains(m, filter_)) continue;
            const bool cur = fs::path(ed.doc().path).filename() == m;
            g.cell();
            const std::string label = std::string(cur ? ICON_CHECK : ICON_MAP_PIN) + "  " + m;
            if (ImGui::Selectable(label.c_str(), cur, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(g.w, 0)) && ImGui::IsMouseDoubleClicked(0))
                ed.openMap(ws.mapsDir() + "/" + m);
            if (ImGui::BeginPopupContextItem()) {
                if (ui::menuItem(ICON_FOLDER_OPEN, "Открыть")) ed.openMap(ws.mapsDir() + "/" + m);
                ImGui::EndPopup();
            }
        }
        if (maps.empty()) ui::emptyStateHere(ICON_MAP_PIN, "В maps/ нет карт", "Файл → Новая карта, затем «Сохранить карту как».");
    }

    void rulesItems(Editor& ed, const char* key, Selection::Kind kind, const char* icon) {
        const Json& R = ed.ws().project.rulesJson;
        ImGui::PushID(key);
        // new item: id field + button on one row
        ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.5f));
        const bool enter = ImGui::InputTextWithHint("##id", "id нового элемента (латиница)", newId_, sizeof newId_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(!newId_[0] || R[key].has(newId_));
        if ((ui::button(ICON_PLUS, "Создать") || enter) && newId_[0] && !R[key].has(newId_)) {
            Json v = Json::object();
            v.set("name", Json::string(newId_));
            if (kind == Selection::Type) v.set("health", Json::parse(R"({"max": 100})"));
            Json prm = Json::object();
            prm.set("path", Json::string(std::string(key) + "." + newId_));
            prm.set("value", v);
            Json r = ed.ws().call("rules_set", prm);
            if (r.has("error")) ed.status = "Ошибка: " + r.getStr("error"); else { ed.reloadRules(); ed.select({kind, -1, newId_}); ed.focusWindow("inspector"); }
            newId_[0] = 0;
        }
        ImGui::EndDisabled();
        if (newId_[0] && R[key].has(newId_)) { ImGui::SameLine(); ui::toneText(ui::Tone::Warn, "такой id уже есть"); }
        Grid g(ImGui::GetFontSize() * 12);
        int shown = 0;
        for (auto& [id, v] : R[key].members()) {
            std::string label = v.getStr("name", id);
            if (!ui::contains(label, filter_) && !ui::contains(id, filter_)) continue;
            ++shown;
            g.cell();
            bool sel = ed.selection.kind == kind && ed.selection.key == id;
            if (ImGui::Selectable((std::string(icon) + "  " + label + "##" + id).c_str(), sel, 0, ImVec2(g.w, 0))) ed.select({kind, -1, id});
            if (ImGui::BeginItemTooltip()) { ImGui::TextUnformatted(label.c_str()); ImGui::TextDisabled("id: %s", id.c_str()); ImGui::EndTooltip(); }
        }
        if (shown == 0) ImGui::TextDisabled(filter_.empty() ? "Пусто" : "Ничего не найдено");
        ImGui::PopID();
    }

    std::string filter_;
    int cat_ = 1;
    char newId_[64] = "";
};

AIVERSE_EDITOR_WINDOW(ProjectWindow);

} // namespace aiverse::editor
