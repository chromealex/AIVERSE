// Playing the project as in the game client. startClientPlay: the GameClient itself hosted in the Game
// window (ClientHost.h). launchClient: the project in a separate AiverseGame window. Both exactly as players get it -
// the game's own menus / meta screens (ui/main.lua), view scripts, script UI, mapClick input and audio.
// The client is the one built next to this editor (same C++ modules: rules "nativeModules"), else the
// regular build's. AIVERSE_CONTENT = project folder; the working directory holds only engine/ (the client
// reads engine assets relative to it, see engineAssetsRoot).
#include <cstdlib>
#include <filesystem>
#include "Editor.h"
#include "core/NativeBuild.h"
#include "core/UiOps.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

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
    if (checkNativeModules(true)) return;   // this build lacks the project's modules: so does its AiverseGame (banner offers a build)
    if (doc().dirty && !doc().path.empty()) saveMap(doc().path);   // the client reads the maps from disk
    std::string exe = !opt_.ownGameExe.empty() ? opt_.ownGameExe : opt_.gameExe;
    if (exe.empty()) { status = "Не найден исполняемый файл игры (AiverseGame) рядом с редактором"; ws_.log("play", status, true); return; }
    if (exe != opt_.ownGameExe && !requiredNativeModules(ws_.project.rulesJson).empty())
        ws_.log("play", "AiverseGame не из сборки этого редактора: модулей C++ проекта в нём может не быть (" + exe + ")", true);
    std::error_code ec;
    const fs::path cwd = engineAssetsRoot(ws_, exe);
    const fs::path logDir = fs::path(projectBuildsTempDir(ws_.project.dir));
    fs::create_directories(logDir, ec);
    const std::string log = (logDir / "client-play.log").string();
#if defined(_WIN32)
    _putenv_s("AIVERSE_CONTENT", ws_.project.dir.c_str());
    const std::string cmd = "start \"\" /D " + shellQuote(cwd.string()) + " " + shellQuote(exe);
#else
    const std::string cmd = "( cd " + shellQuote(cwd.string()) + " && AIVERSE_CONTENT=" + shellQuote(ws_.project.dir) + " " + shellQuote(exe) +
                            " > " + shellQuote(log) + " 2>&1 & )";
#endif
    ws_.log("play", "Запуск клиента игры: " + cmd);
    if (std::system(cmd.c_str()) != 0) { status = "Не удалось запустить клиент игры: " + exe; ws_.log("play", status, true); return; }
    status = "Игра запущена в отдельном окне клиента (журнал: " + log + ")";
}

bool Editor::prefersClientPlay() const {
    // the game's own screens (ui/main.lua) drive the match: the editor's play-test would show a bare map
    std::error_code ec;
    return !ws_.project.dir.empty() && fs::exists(fs::path(ws_.project.dir) / "ui" / "main.lua", ec);
}

void Editor::startClientPlay() {
    if(ws_.project.isFps()){startFpsPlay();return;}
    if (ws_.project.dir.empty()) { status = "Нет открытого проекта"; return; }
    if (checkNativeModules(true)) return;   // the banner offers to build an editor with them
    if (playing()) stopPlay();
    if (doc().dirty && !doc().path.empty()) saveMap(doc().path);   // the client reads the maps from disk
    // engine assets ("engine/..." content paths): the source tree, else the editor's bundled copy
    std::string assets;
    std::error_code ec;
    for (const fs::path& c : {fs::path(opt_.engineDir) / "engine" / "assets", fs::path(platform::contentRoot()) / "engine"})
        if (fs::exists(c / "ui" / "theme.rcss", ec)) { assets = c.string(); break; }
    clientHost_.start(*dev_, ws_.project.dir, assets);
    ws_.log("play", "Игра как в клиенте: " + ws_.project.dir);
    focusWindow("game");
    status = "Игра как в клиенте (меню и скрипты игры)";
}

void Editor::stopClientPlay() {
    if (!clientHost_.active()) return;
    clientHost_.stop();
    status = "Редактирование";
}

} // namespace aiverse::editor
