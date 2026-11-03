// Tools: the current Scene tool (a grid of tool buttons) and its settings (palette of types, terrain
// kinds, brush, levels...). The same tools are on the Scene toolbar.
#include <algorithm>
#include <cctype>
#include "../Editor.h"
#include "../EditorUtil.h"

namespace rts::editor {

namespace {
struct ToolInfo { const char* icon; const char* name; const char* help; };
const ToolInfo kTools[] = {
    {ICON_SELECT, "Выбор", "Клик — выбрать, перетаскивание — двигать, Alt — копия. ПКМ — обзор, ПКМ+WASD — полёт, колесо — масштаб, F — к выбранному."},
    {ICON_OBJECTS, "Объекты", "Клик в «Сцене» ставит выбранный тип. Shift — ставить поверх существующих объектов."},
    {ICON_CELLS, "Клетки", "Рисует тип клеток. Shift — стереть (земля). [ и ] — размер кисти."},
    {ICON_LEVELS, "Уровни", "Плато: юниты переходят между уровнями только по рампам. Высота даёт бонусы из rules.heights."},
    {ICON_HEIGHT, "Высота", "Плавная высота для вида. Shift — опускать."},
    {ICON_FIELDS, "Ресурсы", "Поля ресурсов (руда, спайс): плотность 1–9. Shift — стереть."},
    {ICON_REGIONS, "Регионы", "Регионы для захвата территории: буква региона и название."},
    {ICON_ZONES, "Зоны", "Протяните прямоугольник — получится зона для триггеров и волн."},
    {ICON_STARTS, "Старты", "Клик — стартовая точка игрока: там появятся стартовые юниты фракции."},
    {ICON_ROADS, "Дороги", "Клик по объекту A, затем по B — дорога. Shift — продолжить цепочку."},
    {ICON_ERASER, "Ластик", "Клик по объекту или дороге удаляет их."},
};
static_assert(sizeof kTools / sizeof *kTools == size_t(Tool::Count));
} // namespace

class ToolsWindow : public EditorWindow {
public:
    const char* id() const override { return "tools"; }
    const char* title() const override { return "Инструменты"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "left-bottom"; }

    void draw(Editor& ed) override {
        toolGrid(ed);
        if (int(ed.tool) >= 0 && int(ed.tool) < int(Tool::Count)) ui::hint(kTools[int(ed.tool)].help);
        else ui::hint("Кисть другого окна (поля, рельеф) рисует в «Сцене».");
        ImGui::Spacing();
        auto& d = ed.doc().data;
        const auto& rules = ed.rules();
        auto brushRow = [&] {
            ui::prop("Кисть", "Радиус кисти в клетках. [ и ] в «Сцене» — меньше / больше.");
            ImGui::SliderInt("##brush", &ed.brush, 1, 16, "%d клет.");
        };
        switch (ed.tool) {
        case Tool::Place: {
            if (ui::beginProps("##place")) {
                ui::prop("Владелец");
                std::string cur = ownerName(ed, ed.placeOwner);
                if (ImGui::BeginCombo("##owner", cur.c_str())) {
                    if (ImGui::Selectable("Нейтральный", ed.placeOwner == 0)) ed.placeOwner = 0;
                    if (ImGui::Selectable("Враждебные монстры", ed.placeOwner == -1)) ed.placeOwner = -1;
                    for (size_t p = 0; p < d.players.size(); ++p) if (ImGui::Selectable(d.players[p].name.c_str(), ed.placeOwner == int(p + 1))) ed.placeOwner = int(p + 1);
                    ImGui::EndCombo();
                }
                ui::endProps();
            }
            ui::searchField("##flt", filter_, "Тип объекта");
            ImGui::BeginChild("##palette", ImVec2(0, 0), ImGuiChildFlags_Borders);
            static const char* groups[] = {"Юниты", "Здания", "Ресурсы и декор"};
            static const char* groupIcons[] = {ICON_SWORDS, ICON_CASTLE, ICON_TREES};
            for (int group = 0; group < 3; ++group) {
                std::vector<size_t> items;
                for (size_t i = 0; i < rules.types.size(); ++i) {
                    const auto& ty = rules.types[i];
                    int g = ty.resourceNode >= 0 || (ty.invulnerable && ty.garrisonMax == 0) ? 2 : ty.structure ? 1 : 0;
                    if (g == group && (ui::contains(ty.name, filter_) || ui::contains(ty.id, filter_))) items.push_back(i);
                }
                if (items.empty()) continue;
                char head[96];
                std::snprintf(head, sizeof head, "%s  %s  (%zu)###g%d", groupIcons[group], groups[group], items.size(), group);
                if (!ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) continue;
                for (size_t i : items) {
                    const auto& ty = rules.types[i];
                    if (ImGui::Selectable((ty.name + "##" + ty.id).c_str(), ed.placeType == int(i))) ed.placeType = int(i);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nid: %s", ty.name.c_str(), ty.id.c_str());
                    // the id, dim, right-aligned when there is room
                    const float idw = ImGui::CalcTextSize(ty.id.c_str()).x;
                    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                    if (mx.x - mn.x > ImGui::CalcTextSize(ty.name.c_str()).x + idw + 24)
                        ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - idw - 4, mn.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), ty.id.c_str());
                }
            }
            if (rules.types.empty()) ui::emptyStateHere(ICON_OBJECTS, "В правилах нет типов", "Добавьте тип в окне «Проект».");
            ImGui::EndChild();
            return;   // the palette fills the window
        }
        case Tool::Terrain: {
            static const struct { char c; const char* name; const char* tip; } cells[] = {
                {'.', "Земля", nullptr}, {'~', "Вода", nullptr}, {'#', "Скалы", "Непроходимо"}, {',', "Без стройки", "Проходимо, но строить нельзя"},
                {'=', "Брод", "Проходимая вода"}, {'/', "Рампа", "Подъём между уровнями"}, {'R', "Камень", "Dune: стройка только на камне"}};
            ui::section(nullptr, "Тип клеток");
            for (auto& c : cells) {
                if (ImGui::RadioButton(c.name, ed.paintCell == c.c)) ed.paintCell = c.c;
                if (c.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.tip);
            }
            if (ui::beginProps("##terrain")) { brushRow(); ui::endProps(); }
            break;
        }
        case Tool::Level:
            if (ui::beginProps("##level")) {
                ui::prop("Уровень", "0 — низ, 9 — самое высокое плато");
                ImGui::SliderInt("##lvl", &ed.paintLevel, 0, 9);
                brushRow();
                ui::endProps();
            }
            break;
        case Tool::Height:
            if (ui::beginProps("##height")) { brushRow(); ui::endProps(); }
            break;
        case Tool::Field:
            if (rules.resources.empty()) { ui::callout(ui::Tone::Warn, "В правилах нет ресурсов"); break; }
            ed.fieldRes = std::clamp(ed.fieldRes, 0, int(rules.resources.size()) - 1);
            if (ui::beginProps("##field")) {
                ui::prop("Ресурс");
                if (ImGui::BeginCombo("##res", rules.resources[size_t(ed.fieldRes)].name.c_str())) {
                    for (size_t r = 0; r < rules.resources.size(); ++r) if (ImGui::Selectable(rules.resources[r].name.c_str(), ed.fieldRes == int(r))) ed.fieldRes = int(r);
                    ImGui::EndCombo();
                }
                ui::prop("Плотность");
                ImGui::SliderInt("##amt", &ed.fieldAmount, 1, 9);
                brushRow();
                ui::endProps();
            }
            if (!rules.resources[size_t(ed.fieldRes)].field) ui::callout(ui::Tone::Warn, "У ресурса нет \"field\": true — поле не будет расти и добываться как поле.");
            break;
        case Tool::Region: {
            if (ui::beginProps("##region")) {
                ui::prop("Буква", "Регион — набор клеток с одной буквой (A–Z)");
                char buf[2] = {ed.regionLetter, 0};
                if (ImGui::InputText("##letter", buf, 2) && buf[0]) ed.regionLetter = char(std::toupper(static_cast<unsigned char>(buf[0])));
                ui::prop("Название");
                std::string name = d.regionNames.count(ed.regionLetter) ? d.regionNames[ed.regionLetter] : "";
                if (ui::inputString("##rname", name)) { d.regionNames[ed.regionLetter] = name; ed.doc().touched(); }
                brushRow();
                ui::endProps();
            }
            break;
        }
        case Tool::Start:
            ui::section(nullptr, "Чей старт");
            for (size_t p = 0; p < d.players.size(); ++p) {
                ImGui::RadioButton((d.players[p].name + "##st" + std::to_string(p)).c_str(), &ed.startPlayer, int(p + 1));
                if (d.players[p].hasStart) { ImGui::SameLine(); ImGui::TextDisabled(ICON_CHECK " задан"); }
            }
            break;
        default: break;
        }
        ImGui::Spacing();
        ImGui::Checkbox(ICON_MAGNET "  Привязка к сетке", &ed.snap);
    }

private:
    static std::string ownerName(Editor& ed, int owner) {
        auto& d = ed.doc().data;
        return owner == 0 ? "Нейтральный" : owner < 0 ? "Враждебные монстры" : owner <= int(d.players.size()) ? d.players[size_t(owner - 1)].name : "?";
    }

    // Tool buttons as a grid of icon + name tiles; the columns follow the window width.
    void toolGrid(Editor& ed) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float gap = 4;
        const int cols = std::clamp(int((avail + gap) / 92.0f), 2, int(Tool::Count));
        const float w = (avail - gap * float(cols - 1)) / float(cols), h = ImGui::GetFrameHeight() + 2;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        for (int i = 0; i < int(Tool::Count); ++i) {
            if (i % cols) ImGui::SameLine();
            const bool on = ed.tool == Tool(i);
            if (on) {
                const ImVec4 a = ui::accentColor();
                ImGui::PushStyleColor(ImGuiCol_Button, a);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            std::string label = std::string(kTools[i].icon) + "  " + kTools[i].name + "##tool" + std::to_string(i);
            if (ImGui::Button(label.c_str(), ImVec2(w, h))) { ed.tool = Tool(i); ed.roadStart = -1; }
            if (on) ImGui::PopStyleColor(4);
            if (i < 10 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s — клавиша %d в «Сцене»", kTools[i].name, (i + 1) % 10);
        }
        ImGui::PopStyleVar(2);
    }

    std::string filter_;
};

RTS_EDITOR_WINDOW(ToolsWindow);

} // namespace rts::editor
