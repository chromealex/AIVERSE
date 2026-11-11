// Rules: game-wide settings (victory, heights, regions, upkeep, auto-build, wave clock, ...),
// rules-level triggers, or the whole rules.json as text. Items (types etc.) are edited in the Inspector.
#include "../Editor.h"
#include "../EditorGenreUi.h"
#include "../EditorUtil.h"

namespace aiverse::editor {

class RulesWindow : public EditorWindow {
public:
    const char* id() const override { return "rules"; }
    const char* title() const override { return "Правила"; }
    const char* group() const override { return "logic"; }
    int order() const override { return 1; }
    const char* dockArea() const override { return "right-bottom"; }

    void draw(Editor& ed) override {
        if(ed.ws().project.isFps()) {
            ImGui::TextWrapped("%s",genreText(ed.ws(),"fps.editor.hint").c_str());
            if(ImGui::Button(genreText(ed.ws(),"fps.editor.open").c_str()))ed.focusWindow("fps");
            return;
        }
        static const char* modes[] = {"Общие", "Триггеры", "Весь rules.json"};
        if (ui::segmented("##m", mode_, modes, 3)) message_.clear();
        Json& R = ed.ws().project.rulesJson;
        Json current;
        if (mode_ == 0) {
            current = Json::object();
            for (auto& [k, v] : R.members()) if (k != "types" && k != "resources" && k != "techs" && k != "factions" && k != "triggers") current.set(k, v);
            ui::hint("supplyCap, maxQueue, costModel, victory, heights, regions, upkeep, autoBuild, waveClock, damageTable… Типы, ресурсы, техи и фракции — в «Проекте» и «Инспекторе».");
        } else if (mode_ == 1) {
            current = R["triggers"].isNull() ? Json::array() : R["triggers"];
            ui::hint("Триггеры правил работают на всех картах игры. Триггеры одной карты — в «Иерархии».");
        } else ui::hint("Весь файл правил. Ошибка в JSON или в правилах не сохраняется — файл останется прежним.");
        if (mode_ == 0) damageMatrix(R);
        std::string key = std::to_string(mode_) + "\n" + current.dump();
        if (key != textFor_) { text_ = current.dump(); textFor_ = key; }
        const bool dirty = text_ != current.dump();
        std::string parseError;
        if (dirty) { try { (void)Json::parse(text_); } catch (const std::exception& e) { parseError = e.what(); } }
        ui::inputMultiline("##t", text_, ImVec2(-1, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2 - (message_.empty() ? 6 : 40)));
        if (!parseError.empty()) ui::toneText(ui::Tone::Error, "%s JSON: %s", ICON_ALERT, parseError.c_str());
        else if (dirty) ui::toneText(ui::Tone::Warn, "%s Есть несохранённые изменения", ICON_PENCIL);
        else ui::toneText(ui::Tone::Dim, "%s Совпадает с rules.json", ICON_CHECK);
        ImGui::BeginDisabled(!dirty || !parseError.empty());
        if (ui::wideButton(ICON_SAVE, "Сохранить", 0, 2, true)) {
            try {
                Json v = Json::parse(text_);
                Json backup = R;
                if (mode_ == 0) for (auto& [k, x] : v.members()) R.set(k, x);
                else if (mode_ == 1) R.set("triggers", v);
                else R = v;
                if (!ed.ws().project.reloadRules()) { message_ = "Ошибка: " + ed.ws().project.rulesError; R = backup; ed.ws().project.reloadRules(); }
                else { std::string err; ed.ws().project.saveRules(&err); message_ = err.empty() ? "Сохранено в rules.json" : "Ошибка: " + err; ed.reloadRules(); }
                textFor_.clear();
            } catch (const std::exception& e) { message_ = std::string("JSON: ") + e.what(); }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Проверить правила и записать в rules.json (с ошибкой файл не меняется)");
        ImGui::BeginDisabled(!dirty);
        if (ui::wideButton(ICON_UNDO, "Отменить правку", 1, 2)) textFor_.clear();
        ImGui::EndDisabled();
        ui::resultMessage(message_);
    }

private:
    // damageTable as a matrix (attack type × armor): faster to read than nested JSON; edits stay in the JSON below
    static void damageMatrix(const Json& R) {
        const Json& dt = R["damageTable"];
        const Json& armors = R["armorTypes"];
        if (!dt.isObject() || dt.size() == 0 || !armors.isArray() || armors.size() == 0) return;
        if (!ui::collapsing("Таблица урона (атака × броня)", ImGuiTreeNodeFlags_DefaultOpen)) return;
        const int cols = int(armors.size()) + 1;
        if (cols > 63) return;
        if (ImGui::BeginTable("##dmg", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("");
            for (size_t a = 0; a < armors.size(); ++a) ImGui::TableSetupColumn(armors[a].str().c_str());
            ImGui::TableHeadersRow();
            for (const auto& [attack, row] : dt.members()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(attack.c_str());
                for (size_t a = 0; a < armors.size(); ++a) {
                    ImGui::TableNextColumn();
                    const double k = row[armors[a].str()].toDouble(1.0);
                    const ui::Tone t = k > 1.001 ? ui::Tone::Ok : k < 0.999 ? ui::Tone::Error : ui::Tone::Dim;
                    ui::toneText(t, "%.2f", k);
                }
            }
            ImGui::EndTable();
        }
        ui::hint("Зелёный — больше урона, красный — меньше, 1.00 — без изменений. Править — в JSON ниже.");
    }

    int mode_ = 0;
    std::string text_, textFor_, message_;
};

AIVERSE_EDITOR_WINDOW(RulesWindow);

} // namespace aiverse::editor
