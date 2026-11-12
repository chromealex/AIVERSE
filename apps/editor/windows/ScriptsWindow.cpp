// Scripts: the open game's Lua files (docs/scripting.md), the script log of the running play-test and
// hot reload: when a .lua file under scripts/ changes on disk, the play-test restarts with the new
// scripts (the simulation VM is created from the sources at setup, so a restart is the reload).
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include "../Editor.h"
#include "../EditorIcons.h"
#include "../EditorUtil.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/view/RtsViewScript.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

class ScriptsWindow : public EditorWindow {
public:
    const char* id() const override { return "scripts"; }
    const char* title() const override { return "Скрипты"; }
    const char* group() const override { return "logic"; }
    const char* dockArea() const override { return "bottom"; }
    int order() const override { return 6; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        const auto& rules = ed.ws().project.rulesJson;
        const Json map = ed.doc().toJson();
        auto sims = game::scriptFiles(rules, map, false), views = game::scriptFiles(rules, map, true);
        if (sims.empty() && views.empty()) {
            ui::callout(ui::Tone::Normal, "У игры нет скриптов. Скрипт на Lua добавляет правила: события, таймеры, условия победы, сообщения игрокам.");
            if (ui::button(ICON_PLUS, "Создать scripts/main.lua", true)) {
                Json p = Json::object();
                p.set("path", Json::string("scripts/main.lua"));
                p.set("text", Json::string("-- Скрипт правил игры. Справка: docs/scripting.md\n\n"
                                           "function on_start()\n  game.message(0, \"Игра началась!\")   -- 0 = всем игрокам\nend\n\n"
                                           "on(\"unit_died\", function(e)\n  -- e.owner, e.type, e.killer...\nend)\n"));
                p.set("register", Json::fromBool(true));
                const Json r = ed.ws().call("script_write", p);
                created_ = r.has("error") ? "Ошибка: " + r.getStr("error") : "Создан scripts/main.lua и добавлен в rules.json";
            }
            ImGui::SetItemTooltip("Файл с обработчиками on_start и unit_died, сразу подключён в rules.json \"scripts\"");
            ui::resultMessage(created_);
        }
        if (!sims.empty() || !views.empty()) {
            if (ImGui::BeginTable("##files", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (auto& f : sims) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted((std::string(ICON_BRACES "  ") + f).c_str()); ImGui::TableNextColumn(); ImGui::TextDisabled("симуляция"); }
                for (auto& f : views) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted((std::string(ICON_EYE "  ") + f).c_str()); ImGui::TableNextColumn(); ImGui::TextDisabled("вид"); }
                ImGui::EndTable();
            }
        }
        ui::checkbox("Перезапускать тест при изменении файлов", &autoRestart_);
        auto* m = ed.simModule();
        auto* se = m ? m->scripts() : nullptr;
        ui::section(ICON_SCROLL_TEXT, "Журнал теста");
        if (!se) { ui::hint(ed.playing() ? "В тесте нет скриптов симуляции" : "Журнал появится во время теста (F5)"); return; }
        if (se->errorCount() > 0) { char t[48]; std::snprintf(t, sizeof t, "%s ошибок: %d", ICON_ALERT, se->errorCount()); ui::badge(t, ui::toneColor(ui::Tone::Error)); }
        else ui::toneText(ui::Tone::Ok, "%s Ошибок нет", ICON_CIRCLE_CHECK);
        ImGui::SameLine();
        ui::checkbox("Только ошибки", &errorsOnly_);
        ImGui::BeginChild("##scriptlog", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        for (auto& e : se->log()) {
            if (errorsOnly_ && !e.error) continue;
            ImGui::TextDisabled("%6u", e.tick);
            ImGui::SameLine();
            if (e.error) ui::toneText(ui::Tone::Error, "%s", e.text.c_str());
            else ImGui::TextUnformatted(e.text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    void update(Editor& ed, double dt) override {
        // script messages of a new play-test go to the activity feed / console
        auto* m = ed.simModule();
        auto* se = m ? m->scripts() : nullptr;
        const bool restarted = ed.sim() && ed.sim()->tick() < lastTick_;   // same address, new match
        lastTick_ = ed.sim() ? ed.sim()->tick() : 0;
        if (ed.sim() != viewSim_ || restarted) {   // a new play-test: view scripts for it
            viewSim_ = ed.sim();
            attached_ = nullptr;
            view_.detach();
            ed.paintLayers.reset();ed.entityFx.reset();ed.scriptTargeting={};
            if (viewSim_) {
                const Json map = ed.doc().toJson();
                view_.setContentReader([dir=ed.ws().project.dir](const std::string& path,std::string& text){std::ifstream f(fs::path(dir)/path,std::ios::binary);if(!f)return false;text.assign(std::istreambuf_iterator<char>(f),{});return true;});
                if (view_.load(*viewSim_, ed.ws().project.simRules(map), map, uint8_t(std::max(0, ed.humanSlot())))) view_.attach(*viewSim_);
                for (auto& e : view_.log()) if (e.error) ed.ws().log("view-script", e.text, true);
            }
        }
        if (viewSim_ && view_.active()) {
            const size_t before = view_.log().size();
            view_.setTargeting(ed.scriptTargeting);
            view_.update(*viewSim_, float(dt));
            ed.paintLayers = view_.paintLayers();
            ed.entityFx = view_.entityFx();
            for (size_t i = before; i < view_.log().size(); ++i) ed.ws().log("view-script", view_.log()[i].text, view_.log()[i].error);
            for (auto& r : view_.takeRequests())
                if (r.kind == game::ViewRequest::Notice) ed.status = r.text;
                else if (r.kind == game::ViewRequest::Effect) ed.gameFx().play(r.text, {r.x, r.y, r.z});
        }
        if (se != attached_) {
            attached_ = se;
            if (se) {
                Workspace* ws = &ed.ws();
                for (auto& e : se->log()) if (e.error) ws->log("script", e.text, true);
                se->onLog = [ws](const game::ScriptLogEntry& e) { if (e.error) ws->log("script", e.text, true); };
            }
        }
        // hot reload: poll scripts/**/*.lua once per second
        since_ += dt;
        if (since_ < 1.0) return;
        since_ = 0;
        const std::string& dir = ed.ws().project.dir;
        if (dir != dir_) { dir_ = dir; stamps_.clear(); }
        if (dir.empty()) return;
        std::error_code ec;
        const fs::path root = fs::path(dir) / "scripts";
        if (!fs::exists(root, ec)) return;
        std::vector<std::string> changed;
        for (auto& f : fs::recursive_directory_iterator(root, ec)) {
            if (!f.is_regular_file(ec) || f.path().extension() != ".lua") continue;
            const auto t = fs::last_write_time(f.path(), ec);
            if (ec) continue;
            const std::string key = f.path().generic_string();
            auto it = stamps_.find(key);
            if (it == stamps_.end()) stamps_[key] = t;
            else if (it->second != t) { it->second = t; changed.push_back(fs::relative(f.path(), dir, ec).generic_string()); }
        }
        if (changed.empty()) return;
        for (auto& c : changed) {
            std::string text, err;
            std::ifstream in(fs::path(dir) / c, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), {});
            err = game::checkScriptSyntax(c, text, c.rfind("scripts/view/", 0) != 0);
            ed.ws().log("script", err.empty() ? "изменён " + c : err, !err.empty());
        }
        if (autoRestart_ && ed.playing()) {
            const int slot = ed.humanSlot();
            ed.stopPlay();
            ed.startPlay(slot);
            ed.status = "Скрипты перезагружены: тест перезапущен";
        }
    }

private:
    bool autoRestart_ = true, errorsOnly_ = false;
    std::string created_;
    double since_ = 0;
    std::string dir_;
    game::ScriptEngine* attached_ = nullptr;
    Simulation* viewSim_ = nullptr;
    uint32_t lastTick_ = 0;
    game::ViewScripts view_;
    std::map<std::string, fs::file_time_type> stamps_;
};

AIVERSE_EDITOR_WINDOW(ScriptsWindow);

} // namespace aiverse::editor
