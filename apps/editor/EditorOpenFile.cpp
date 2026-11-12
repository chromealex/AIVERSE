// Opening projects by .aiverse: requests from the OS (double-click / Open With, macOS openURLs ->
// EventType::OpenFile), the hub and the menus go through requestOpenProject, which asks to save
// unsaved map / rules changes before switching. Also the .aiverse file association (FileAssoc.h).
#include <filesystem>
#include "Editor.h"
#include "EditorIcons.h"
#include "EditorUtil.h"
#include "core/ProjectFile.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

void Editor::initFileAssoc() {
    setFileAssocLogo((fs::path(opt_.engineDir) / "apps" / "editor" / "branding" / "icon_1024.png").string());
    // a per-project build with the project's C++ modules (EditorNativeBuild.cpp) never takes over .aiverse
    if (projectBuild()) { assoc_ = {}; assoc_.detail = "редактор собран для одного проекта: .aiverse открывает основной редактор"; return; }
    const std::string done = ensureFileAssociation(opt_.editorExe);
    if (!done.empty()) ws_.log("editor", done);
    assoc_ = fileAssociationStatus(opt_.editorExe);
}

void Editor::registerFileAssoc() {
    if (projectBuild()) {
        status = "Это редактор, собранный для одного проекта: файлы .aiverse остаются за основным редактором";
        ws_.log("editor", status, true);
        return;
    }
    std::string msg;
    const bool ok = registerFileAssociation(opt_.editorExe, &msg);
    ws_.log("editor", msg, !ok);
    status = msg;
    assoc_ = fileAssociationStatus(opt_.editorExe);
}

void Editor::requestOpenProject(const std::string& path) {
    pendingOpen_ = path;
    openAsked_ = false;
}

// Body of an "unsaved changes" modal (inside BeginPopupModal): lists what is unsaved, Save / Don't save /
// Cancel. 0 = no answer yet, 1 = go on (saved or discarded), 2 = cancel. Also used before a restart
// into the per-project editor build (EditorNativeBuild.cpp).
int Editor::drawUnsavedChanges(const std::string& question) {
    ImGui::Text(ICON_WARNING "  В проекте «%s» есть несохранённые изменения:", ws_.project.manifest.getStr("name", "без имени").c_str());
    if (doc().dirty) ImGui::BulletText("карта %s", doc().path.empty() ? "(без имени)" : ws_.relative(doc().path).c_str());
    if (ws_.project.rulesDirty) ImGui::BulletText("правила (%s)", fs::path(ws_.project.rulesPath()).filename().string().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted(question.c_str());
    ImGui::Spacing();
    bool go = false, close = false;
    const bool canSave = !doc().dirty || !doc().path.empty();   // an unnamed map needs the Save As dialog of the Map menu
    ImGui::BeginDisabled(!canSave);
    if (ui::button(ICON_SAVE, "Сохранить", true, ImVec2(140, 0))) {
        bool ok = true;
        if (doc().dirty) ok = saveMap(doc().path);
        if (ok && ws_.project.rulesDirty) {
            std::string err;
            ok = ws_.project.saveRules(&err);
            if (!ok) status = "Ошибка сохранения правил: " + err;
        }
        go = ok;
        close = !ok;
    }
    ImGui::EndDisabled();
    if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Карта ещё без имени: сохраните её через «Карта → Сохранить как»");
    ImGui::SameLine();
    if (ImGui::Button("Не сохранять", ImVec2(140, 0))) go = true;
    ImGui::SameLine();
    if (ImGui::Button("Отмена", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    return go ? 1 : close ? 2 : 0;
}

void Editor::drawOpenRequest() {
    if (pendingOpen_.empty()) return;
    static std::string target, targetName;   // resolved folder of the pending request
    const char* kPopup = "Несохранённые изменения";
    if (!openAsked_) {
        std::string err;
        target = resolveProjectDir(pendingOpen_, &err);
        if (target.empty()) {
            status = "Не удалось открыть проект: " + err;
            ws_.log("editor", status, true);
            pendingOpen_.clear();
            return;
        }
        std::error_code ec;
        if (!ws_.project.dir.empty() && fs::equivalent(target, ws_.project.dir, ec)) {   // e.g. the file the editor was started with
            showPicker_ = false;
            pendingOpen_.clear();
            return;
        }
        const bool unsaved = doc().dirty || ws_.project.rulesDirty;
        if (!unsaved) { openGame(pendingOpen_); pendingOpen_.clear(); return; }
        try { targetName = Json::parseFile((fs::path(target) / "game.json").string()).getStr("name", fs::path(target).filename().string()); }
        catch (...) { targetName = fs::path(target).filename().string(); }
        openAsked_ = true;
        ImGui::OpenPopup(kPopup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (openAsked_ && !ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);   // lost (another modal closed it): ask again
        return;
    }
    const int choice = drawUnsavedChanges("Сохранить их перед открытием «" + targetName + "»?");
    const bool go = choice == 1, close = choice == 2;
    if (go || close) {
        ImGui::CloseCurrentPopup();
        if (go) openGame(pendingOpen_);
        pendingOpen_.clear();
        openAsked_ = false;
    }
    ImGui::EndPopup();
}

} // namespace aiverse::editor
