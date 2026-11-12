// AIVERSE editor entry point.
//   aiverse_editor [<project.aiverse | project dir>] [--game <id> | --project <.aiverse | dir with game.json>] [--map <file.json>] [--engine <engine dir>]
//              [--listen <port>|0] [--play]
//              [--native-editor] [--tools-from <dir>]   (set by the switch to a per-project build: EditorNativeBuild.cpp)
// Without --listen, uses the default MCP port if free, otherwise an OS-assigned port.
#include "engine/platform/EngineDir.h"
#include "core/BuildTool.h"
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "Editor.h"

#ifndef AIVERSE_SOURCE_DIR
#define AIVERSE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;
using namespace aiverse;

static std::unique_ptr<editor::Editor> g_editor;
static int g_demoSelect = -1;
static std::string g_demoWindow;
static bool g_demoPlay = false;
static bool g_demoClient = false;     // --play-client: the game as in the client, hosted in the Game window
static std::string g_demoDevice;      // --device id[:portrait|:landscape]: Game window simulator preset

static std::string exeDir(const char* argv0) {
    std::error_code ec;
#if defined(__linux__)
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path().string();
#endif
    fs::path p2 = fs::absolute(fs::path(argv0), ec);
    return p2.parent_path().string();
}

static std::string findNear(const std::string& dir, std::initializer_list<const char*> names) {
    for (const char* n : names)
        for (fs::path c : {fs::path(dir) / n, fs::path(dir) / ".." / ".." / ".." / n}) {
            std::error_code ec;
            if (fs::exists(c, ec)) return fs::canonical(c, ec).string();
        }
    return {};
}

int aiverseMain(int argc, char** argv) {
    editor::Editor::Options opt;
    opt.engineDir = aiverse::platform::resolveEngineDir(AIVERSE_SOURCE_DIR, argv[0]);   // source tree, or a packaged engine
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--play") g_demoPlay = true;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--play-client") g_demoClient = true;
    std::string game, toolsFrom;
    if (const char* e = std::getenv("AIVERSE_NATIVE_EDITOR"); e && *e && std::string(e) != "0") opt.nativeChild = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        // a bare argument is a project to open: "<editor> path/My Game.aiverse" (Windows / Linux
        // double-click, file managers' "Open with") or a project folder; -psn_* is macOS Finder noise
        if (!a.empty() && a[0] != '-') { opt.projectDir = a; continue; }
        if (a == "--native-editor") { opt.nativeChild = true; continue; }
        if (i + 1 >= argc) break;
        if (a == "--project") opt.projectDir = argv[++i];   // folder, .aiverse / legacy .rtsproj or game.json
        else if (a == "--game") game = argv[++i];
        else if (a == "--listen") opt.listenPort = std::atoi(argv[++i]);
        else if (a == "--map") opt.openMap = argv[++i];
        else if (a == "--engine") opt.engineDir = argv[++i];
        else if (a == "--tools-from") toolsFrom = argv[++i];   // aiversebuild / aiversemcp / AiverseGame of the regular build
        else if (a == "--device") g_demoDevice = argv[++i];
        else if (a == "--window") g_demoWindow = argv[++i];   // open + focus a window (automated screenshots)
        else if (a == "--select") g_demoSelect = std::atoi(argv[++i]); // used by automated screenshots
    }
    std::error_code ec;
    if (!game.empty()) opt.projectDir = opt.engineDir + "/templates/" + game;
    if (!opt.projectDir.empty()) opt.projectDir = fs::absolute(opt.projectDir, ec).lexically_normal().string();
    if (!opt.projectDir.empty() && (opt.projectDir.back() == '/' || opt.projectDir.back() == '\\')) opt.projectDir.pop_back();
    if (!opt.openMap.empty()) opt.openMap = fs::absolute(opt.openMap, ec).string();
    // a per-project build has only the editor: the other tools come from the regular build (--tools-from)
    const std::string toolDir = toolsFrom.empty() ? exeDir(argv[0]) : toolsFrom;
    opt.buildTool = editor::findBuildTool(toolDir, opt.engineDir);
    opt.mcpTool = findNear(toolDir, {"aiversemcp", "aiversemcp.exe", "Release/aiversemcp.exe"});
    {
        std::error_code ec2;
#if defined(__linux__)
        fs::path self = fs::read_symlink("/proc/self/exe", ec2);
        if (ec2) self = fs::absolute(argv[0], ec2);
#else
        fs::path self = fs::absolute(argv[0], ec2);
#endif
        opt.editorExe = self.lexically_normal().string();
    }
    opt.gameExe = findNear(toolDir, {"AiverseGame", "AiverseGame.app/Contents/MacOS/AiverseGame", "AiverseGame.exe", "Release/AiverseGame.exe"});
    opt.ownGameExe = findNear(exeDir(argv[0]), {"AiverseGame", "AiverseGame.app/Contents/MacOS/AiverseGame", "AiverseGame.exe", "Release/AiverseGame.exe"});

    g_editor = std::make_unique<editor::Editor>(opt);
    platform::AppConfig cfg;
    cfg.title = "AIVERSE Editor";
    cfg.width = 1440; cfg.height = 900;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) {
        g_editor->init(d);
        if (g_demoSelect >= 0) g_editor->selectEntity(g_demoSelect);
        if (!g_demoDevice.empty()) {
            const size_t c = g_demoDevice.find(':');
            g_editor->gameDevice = g_demoDevice.substr(0, c);
            if (c != std::string::npos) g_editor->gameLandscape = g_demoDevice.substr(c + 1) == "landscape";
        }
        if (g_demoPlay) g_editor->startPlay(0);
        if (g_demoClient) g_editor->startClientPlay();
    };
    cb.frame = [](gfx::Device& d, double dt) {
        static int frames = 0;
        if (++frames == 5 && !g_demoWindow.empty()) {
            if (g_demoWindow == "hub-new") g_editor->showProjectHub(1);
            else if (g_demoWindow == "hub-wizard") g_editor->showProjectHub(2);
            else g_editor->focusWindow(g_demoWindow);
        }   // after the dock layout exists
        g_editor->frame(d, dt);
    };
    cb.event = [](const platform::Event& e) { g_editor->event(e); };
    cb.shutdown = [] { g_editor->shutdown(); };
    return platform::run(cfg, cb);
}
