// "Play in the game client": the project in a separate RTSGame window, exactly as players get it -
// the game's own menus / meta screens (ui/main.lua), view scripts, script UI, mapClick input and audio.
// The client is the one built next to this editor (same C++ modules: rules "nativeModules"), else the
// regular build's. RTS_CONTENT = project folder; the working directory holds only engine/ (the client
// reads engine assets relative to it, see engineAssetsRoot).
#include <cstdlib>
#include <filesystem>
#include "Editor.h"
#include "core/NativeBuild.h"
#include "core/UiOps.h"

namespace fs = std::filesystem;

namespace rts::editor {

namespace {
std::string shellQuote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}
} // namespace

void Editor::launchClient() {
    if (ws_.project.dir.empty()) { status = "Нет открытого проекта"; return; }
    if (checkNativeModules(true)) return;   // this build lacks the project's modules: so does its RTSGame (banner offers a build)
    if (doc().dirty && !doc().path.empty()) saveMap(doc().path);   // the client reads the maps from disk
    std::string exe = !opt_.ownGameExe.empty() ? opt_.ownGameExe : opt_.gameExe;
    if (exe.empty()) { status = "Не найден исполняемый файл игры (RTSGame) рядом с редактором"; ws_.log("play", status, true); return; }
    if (exe != opt_.ownGameExe && !requiredNativeModules(ws_.project.rulesJson).empty())
        ws_.log("play", "RTSGame не из сборки этого редактора: модулей C++ проекта в нём может не быть (" + exe + ")", true);
    std::error_code ec;
    const fs::path cwd = engineAssetsRoot(ws_, exe);
    const fs::path logDir = fs::path(projectBuildsTempDir(ws_.project.dir));
    fs::create_directories(logDir, ec);
    const std::string log = (logDir / "client-play.log").string();
#if defined(_WIN32)
    _putenv_s("RTS_CONTENT", ws_.project.dir.c_str());
    const std::string cmd = "start \"\" /D " + shellQuote(cwd.string()) + " " + shellQuote(exe);
#else
    const std::string cmd = "( cd " + shellQuote(cwd.string()) + " && RTS_CONTENT=" + shellQuote(ws_.project.dir) + " " + shellQuote(exe) +
                            " > " + shellQuote(log) + " 2>&1 & )";
#endif
    ws_.log("play", "Запуск клиента игры: " + cmd);
    if (std::system(cmd.c_str()) != 0) { status = "Не удалось запустить клиент игры: " + exe; ws_.log("play", status, true); return; }
    status = "Игра запущена в отдельном окне клиента (журнал: " + log + ")";
}

} // namespace rts::editor
