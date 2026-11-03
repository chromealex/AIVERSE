// "Build the editor with the project's C++ modules" (docs/native-modules.md § Редактор с модулями
// проекта). A project lists modules in rules.json "nativeModules"; when this binary lacks some of them
// (on open and on Play), a banner offers to configure + build the engine for the project into
// <project>/Builds/.temp/editor-<host> (NativeBuild.h) through the Workspace runner (output in the Console),
// then to restart in that editor on the same project (unsaved changes are asked about first). A build
// started by an agent (MCP native_build) shows up here too.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include "Editor.h"
#include "EditorIcons.h"
#include "EditorUtil.h"

namespace fs = std::filesystem;

namespace rts::editor {

using Stage = NativeBuildUi::Stage;

// started by a switch (--native-editor / RTS_NATIVE_EDITOR) or living in <project>/Builds/.temp/editor-*
bool Editor::projectBuild() const { return opt_.nativeChild || isProjectBuildExe(opt_.editorExe); }

void Editor::refreshNativeBuild(bool withTools) {
    auto& n = native_;
    if (withTools) {
        n.tools = findNativeTools(ws_);   // rtsbuild --check-toolchain: about a second
        n.plan = nativePlanFor(ws_, n.tools);
    }
    n.existing = findNativeBuilt(ws_);
    n.ready = nativeBuildReady(ws_, n.existing, opt_.editorExe, &n.why);
}

bool Editor::checkNativeModules(bool fromPlay) {
    auto& n = native_;
    if (ws_.project.dir.empty()) { n.stage = Stage::Hidden; return false; }
    n.compiled = compiledNativeIds();
    n.missing = missingNativeModules(ws_.project.rulesJson, n.compiled);
    const bool building = ws_.building() && ws_.runLabel() == kNativeBuildLabel;
    if (n.missing.empty()) { if (!building) n.stage = Stage::Hidden; return false; }
    std::string ids;
    for (auto& m : n.missing) ids += (ids.empty() ? "" : ", ") + m;
    // an error only when it stops a Play; on open the switch / banner handles it
    ws_.log("native", "В этом редакторе нет модулей C++ проекта: " + ids, fromPlay);
    if (fromPlay) status = "Нельзя запустить: в редакторе нет модулей C++ проекта (" + ids + ")";
    if (building) { n.stage = Stage::Building; return true; }
    n.projectDir = ws_.project.dir;
    refreshNativeBuild(false);
    if (nativeOpenAction(true, n.ready, projectBuild()) == NativeOpenAction::Switch) {
        const std::string line = "Проект открывается в собранном для него редакторе с модулями C++ (" + ids + "): " + n.existing.launch;
        ws_.log("native", line);
        ws_.appendBuildLog(line);
        status = line;
        n.stage = Stage::Hidden;
        n.restartPending = true;   // asks about unsaved changes first
        n.restartAsked = false;
        return true;
    }
    if (projectBuild()) ws_.log("native", "Этот редактор уже запущен как собранный для проекта: автоматического переключения нет, предлагается пересборка");
    refreshNativeBuild(true);
    if (n.stage != Stage::Done) n.stage = Stage::Offer;
    return true;
}

void Editor::startNativeEditorBuild() {
    auto& n = native_;
    refreshNativeBuild(true);
    if (n.tools.cmake.empty()) { ws_.log("native", "Сборка редактора с модулями проекта: " + n.tools.problem, true); ws_.appendBuildLog("ОШИБКА: " + n.tools.problem); return; }
    if (playing()) stopPlay();
    if (doc().dirty && !doc().path.empty()) saveMap(doc().path);   // as builds do: the new editor opens what is on disk
    if (std::string e = startNativeBuild(ws_, n.plan); !e.empty()) { status = "Сборка редактора: " + e; n.stage = Stage::Failed; return; }
    n.stage = Stage::Building;
    n.userStarted = true;
    status = "Идёт сборка редактора с модулями проекта (ход — в Console)";
    focusWindow("console");
}

void Editor::restartIntoNativeBuild() {
    auto& n = native_;
    std::error_code ec;
    n.existing = findNativeBuilt(ws_);
    if (n.existing.editor.empty()) {
        status = "Нет собранного редактора в " + nativeBuildDir(ws_.project.dir, nativeHost());
        ws_.log("native", status, true);
        ws_.appendBuildLog("ОШИБКА: " + status);
        return;
    }
    if (playing()) stopPlay();
    // rtsbuild / rtsmcp / RTSGame stay those of this (regular) build: the project build has only the editor
    std::string tools;
    if (fs::path bt(opt_.buildTool); bt.is_absolute() && fs::exists(bt, ec)) tools = bt.parent_path().string();
    const std::string arg = ws_.project.file.empty() ? ws_.project.dir : ws_.project.file;
    const std::string cmd = nativeRestartCommand(nativeHost(), n.existing.launch, arg, tools);
    // free the MCP port so the new editor takes the default one and agents reconnect to it
    server_.close();
    ws_.log("native", "Перезапуск в редакторе с модулями проекта: " + cmd);
    if (std::system(cmd.c_str()) != 0) {
        status = "Не удалось запустить собранный редактор: " + n.existing.launch;
        ws_.log("native", status + " (" + cmd + ")", true);
        ws_.appendBuildLog("ОШИБКА: " + status);
        if (opt_.listenPort != 0) { if (opt_.listenPort == -1) server_.listenAvailable(); else server_.listen(opt_.listenPort); }
        return;
    }
    quit_ = true;
}

void Editor::drawNativeBuild() {
    auto& n = native_;
    // a run of the native build finished (started here or by an agent through MCP)
    if (n.seenRuns < 0) n.seenRuns = ws_.runCount();
    if (ws_.building() && ws_.runLabel() == kNativeBuildLabel && n.stage != Stage::Building) {
        n.stage = Stage::Building;
        n.projectDir = ws_.project.dir;
        n.plan = nativePlanFor(ws_, {});   // paths for the banner (a build started by an agent)
    }
    if (!ws_.building() && ws_.runCount() != n.seenRuns) {
        n.seenRuns = ws_.runCount();
        const std::string label = ws_.runLabel();
        if (label == kNativeBuildLabel) {
            refreshNativeBuild(false);
            if (ws_.buildResult() == 0 && !n.existing.editor.empty()) {
                n.stage = Stage::Done;
                ws_.log("native", "Редактор с модулями проекта собран: " + n.existing.editor);
                status = "Редактор с модулями проекта собран";
                // the user asked for this build: switch right away (unsaved changes are asked about first)
                if (n.userStarted && !n.missing.empty()) {
                    const std::string line = "Перезапуск в собранном редакторе с модулями проекта: " + n.existing.launch;
                    ws_.log("native", line);
                    ws_.appendBuildLog(line);
                    n.restartPending = true;
                    n.restartAsked = false;
                }
            } else {
                n.stage = Stage::Failed;
                const std::string e = "Сборка редактора с модулями проекта не удалась (код " + std::to_string(ws_.buildResult()) + "), журнал: " + ws_.lastLogFile();
                ws_.log("native", e, true);
                status = e;
            }
            n.userStarted = false;
        } else if (n.cmakeInstalling && label.rfind("toolchain-", 0) == 0) {
            n.cmakeInstalling = false;
            refreshNativeBuild(true);
            if (ws_.buildResult() != 0) ws_.log("native", "Установка CMake не удалась — подробности в Console", true);
        }
    }

    // restart: ask about unsaved changes first (the same prompt as opening another project)
    if (n.restartPending) {
        const char* kPopup = "Несохранённые изменения##restart";
        if (!n.restartAsked) {
            if (!doc().dirty && !ws_.project.rulesDirty) { n.restartPending = false; restartIntoNativeBuild(); return; }
            n.restartAsked = true;
            ImGui::OpenPopup(kPopup);
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const int choice = drawUnsavedChanges("Сохранить их перед перезапуском в собранном редакторе?");
            if (choice != 0) {
                ImGui::CloseCurrentPopup();
                n.restartPending = n.restartAsked = false;
                if (choice == 1) restartIntoNativeBuild();
            }
            ImGui::EndPopup();
        } else if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    }

    if (n.stage == Stage::Hidden || showPicker_) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 36), ImGuiCond_Always, ImVec2(0.5f, 0));
    const float bannerW = std::min(720.0f, vp->WorkSize.x - 32);   // fixed width: long paths wrap
    ImGui::SetNextWindowSizeConstraints(ImVec2(bannerW, 0), ImVec2(bannerW, FLT_MAX));
    const auto& pal = theme_.palette();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.panelRaised);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.warning);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    const bool visible = ImGui::Begin(ICON_BUILD "  Модули C++ проекта##nativebuild", nullptr, fl);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (!visible) { ImGui::End(); return; }
    const ImVec4 red = pal.danger, yellow = pal.warning, green = pal.success;
    auto join = [](const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += (s.empty() ? "" : ", ") + x; return s.empty() ? std::string("нет") : s; };
    auto folder = [&] {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Папка сборки: %s", (n.plan.buildDir.empty() ? nativeBuildDir(ws_.project.dir, nativeHost()) : n.plan.buildDir).c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Builds/ не попадает в git, паки и копии проекта; основной редактор и движок не меняются");
    };
    if (n.stage == Stage::Offer) {
        ImGui::TextColored(yellow, ICON_WARNING);
        ImGui::SameLine();
        ImGui::TextWrapped("В проекте есть модули C++, которых нет в этом редакторе: %s", join(n.missing).c_str());
        ImGui::TextDisabled("В этот редактор собраны: %s", join(n.compiled).c_str());
        ImGui::TextWrapped("Без них игру не запустить. Редактор можно собрать вместе с модулями проекта (из папки native/) и перезапустить в нём.");
        folder();
        ImGui::Spacing();
        if (n.tools.cmake.empty()) {
            ImGui::TextColored(red, "%s", n.tools.problem.c_str());
            if (n.tools.cmakeAutoInstall) {
                ImGui::BeginDisabled(ws_.building());
                if (ui::button(ICON_DOWNLOAD, "Установить CMake", true)) {
                    try { ws_.startTool("--install-toolchain cmake", "toolchain-cmake", false); n.cmakeInstalling = true; focusWindow("console"); }
                    catch (const std::exception& e) { ws_.log("native", std::string("Установка CMake: ") + e.what(), true); ws_.appendBuildLog(std::string("ОШИБКА: ") + e.what()); }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (n.cmakeInstalling) { ImGui::TextColored(yellow, ICON_REFRESH " идёт установка — ход в Console"); ImGui::SameLine(); }
            }
            if (ImGui::Button("Проверить снова")) refreshNativeBuild(true);
            ImGui::SameLine();
        } else if (n.ready) {
            ImGui::TextColored(green, ICON_CHECK "  Редактор с модулями проекта уже собран и актуален.");
            if (ui::button(ICON_REFRESH, "Перезапустить в собранном редакторе", true)) { n.restartPending = true; n.restartAsked = false; }
            ImGui::SameLine();
            ImGui::BeginDisabled(ws_.building());
            if (ImGui::Button("Собрать заново")) startNativeEditorBuild();
            ImGui::EndDisabled();
            ImGui::SameLine();
        } else {
            if (!n.existing.editor.empty()) ImGui::TextDisabled("Собранный ранее редактор не подходит: %s", n.why.c_str());
            ImGui::BeginDisabled(ws_.building());
            if (ui::button(ICON_BUILD, "Собрать редактор с модулями проекта", true)) startNativeEditorBuild();
            ImGui::EndDisabled();
            if (ws_.building() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Идёт другая сборка (%s)", ws_.runLabel().c_str());
            ImGui::SameLine();
        }
        if (ImGui::Button("Отмена")) n.stage = Stage::Hidden;
    } else if (n.stage == Stage::Building) {
        ImGui::TextColored(yellow, ICON_REFRESH "  Идёт сборка редактора с модулями проекта: %s", join(n.missing).c_str());
        ImGui::TextWrapped("Первая сборка занимает несколько минут; ход — в окне Console. Редактором можно пользоваться.");
        folder();
        if (ImGui::Button(ICON_CONSOLE " Показать Console")) focusWindow("console");
        ImGui::SameLine();
        if (ImGui::Button("Скрыть")) n.stage = Stage::Hidden;   // the result still shows when the build ends
    } else if (n.stage == Stage::Done) {
        ImGui::TextColored(green, ICON_CHECK "  Редактор с модулями проекта собран.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", n.existing.launch.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Перезапустить в нём этот проект? Этот редактор закроется; несохранённые изменения можно будет сохранить.");
        if (ui::button(ICON_REFRESH, "Перезапустить в собранном редакторе", true)) { n.restartPending = true; n.restartAsked = false; }
        ImGui::SameLine();
        if (ImGui::Button("Позже")) n.stage = Stage::Hidden;
    } else if (n.stage == Stage::Failed) {
        ImGui::TextColored(red, ICON_ERROR "  Сборка редактора с модулями проекта не удалась.");
        ImGui::TextWrapped("Подробности — в окне Console и в журнале %s", ws_.lastLogFile().empty() ? "logs/editor.log" : ws_.lastLogFile().c_str());
        if (ImGui::Button(ICON_CONSOLE " Показать Console")) focusWindow("console");
        ImGui::SameLine();
        ImGui::BeginDisabled(ws_.building());
        if (ui::button(ICON_REFRESH, "Повторить", true)) startNativeEditorBuild();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Закрыть")) n.stage = Stage::Hidden;
    }
    ImGui::End();
}

} // namespace rts::editor
