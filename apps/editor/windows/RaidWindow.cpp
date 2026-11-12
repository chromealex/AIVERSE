// Extraction raid (docs/raid.md): the raid of the open game (rules.json "raid" + the map's "raid" block) -
// mode, clock, extraction points, objectives, interactables - and a bot raid on the open map with its receipts.
// Agents use raid_info / raid_set / raid_point / raid_run (core/RaidOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/RaidOps.h"

namespace aiverse::editor {

class RaidWindow : public EditorWindow {
public:
    const char* id() const override { return "raid"; }
    const char* title() const override { return "Рейд"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 7; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_PACKAGE, "Игра не открыта", "Файл → Открыть проект"); return; }
        Json info;
        try { info = raidInfo(ws); }
        catch (const std::exception& e) { ui::callout(ui::Tone::Error, std::string("Ошибка в рейде: ") + e.what()); return; }
        if (!info.getBool("enabled")) {
            ui::emptyStateHere(ICON_PACKAGE, "В игре нет рейда",
                               "Рейд — цикл экстракшен-шутера: лут в контейнерах, задания, точки эвакуации и квитанции для меты.",
                               ImGui::GetContentRegionAvail().y * 0.6f);
            ui::hint("Блок rules.json \"raid\" (docs/raid.md), пример — шаблон «Extraction Raid».");
            return;
        }
        if (ui::beginProps("##raid")) {
            ui::prop("Режим");
            ImGui::TextDisabled("%s, %.0f с", info.getStr("mode").c_str(), info["duration"].toDouble());
            ui::prop("Рейдеры на карте");
            ImGui::TextDisabled("%lld", (long long)info.getInt("raidersOnMap"));
            ui::prop("Объекты");
            ImGui::TextDisabled("%lld", (long long)info.getInt("interactablesOnMap"));
            ui::endProps();
        }
        for (auto& wmsg : info["warnings"].items()) ui::callout(ui::Tone::Warn, wmsg.str());
        ui::section(ICON_MAP_PIN, "Точки эвакуации");
        for (auto& p : info["points"].items()) {
            std::string req;
            for (auto& q : p["requires"].items()) req += (req.empty() ? " после: " : ", ") + q.str();
            ImGui::BulletText("%s  (%.1f, %.1f)  r=%.1f  канал %.1f с%s%s", p.getStr("id").c_str(), p["pos"][size_t(0)].toDouble(),
                              p["pos"][size_t(1)].toDouble(), p["radius"].toDouble(), p["channel"].toDouble(),
                              p.getBool("call") ? "  вызов" : "", req.c_str());
        }
        ui::section(ICON_PACKAGE, "Задания");
        for (auto& o : info["objectives"].items())
            ImGui::BulletText("%s — %s%s", o.getStr("id").c_str(), o.getStr("text").c_str(), o.getBool("required") ? " (обязательно)" : "");
        if (ui::wideButton(ICON_PLAY, "Прогнать рейд ботом")) {
            try {
                const Json r = raidRun(ws, uint64_t(seed_), 900);
                result_.clear();
                for (auto& rc : r["receipts"].items())
                    result_ += "Игрок " + std::to_string(rc.getInt("player")) + ": " + rc.getStr("outcome") + ", ценность " +
                               std::to_string(rc.getInt("value")) + "\n";
                result_ += "Время: " + std::to_string(int(r["seconds"].toDouble())) + " с";
            } catch (const std::exception& e) { result_ = std::string("Ошибка: ") + e.what(); }
            ++seed_;
        }
        if (!result_.empty()) ImGui::TextWrapped("%s", result_.c_str());
    }

private:
    std::string result_;
    int seed_ = 1;
};

AIVERSE_EDITOR_WINDOW(RaidWindow);

} // namespace aiverse::editor
