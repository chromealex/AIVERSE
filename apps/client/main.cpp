// Entry point of the game client (AiverseGame): command line, window settings, splash -> GameClient.
// The client itself (GameClient*.cpp, Client*.cpp) is also hosted by the editor's Game window.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>

#include "engine/platform/GraphicsSettings.h"
#include "engine/platform/WindowSettings.h"
#include "ClientSplash.h"
#include "HostBoot.h"
#include "engine/platform/EngineDir.h"
#include "GameClient.h"

using namespace aiverse;
using platform::Event;

namespace aiverse::client {
GameClient* g_client = nullptr;
}

#if defined(AIVERSE_CLIENT_ACTOR)
int actorClientMain(int argc, char** argv);   // apps/fps/client.cpp
#endif
#if defined(AIVERSE_CLIENT_VOXEL)
int voxelClientMain(int argc, char** argv);   // apps/voxel/client.cpp
#endif

namespace {
// The project's game.json "module": --project DIR, else the content root.
std::string projectModule(int argc, char** argv) {
    std::string text;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--project") {
            std::ifstream f(std::string(argv[i + 1]) + "/game.json");
            std::stringstream ss; ss << f.rdbuf(); text = ss.str();
        }
    if (text.empty() && !platform::readContentFile("game.json", text)) return "";
    try { return Json::parse(text).getStr("module", ""); } catch (const std::exception&) { return ""; }
}
}

int aiverseMain(int argc, char** argv) {
    using namespace aiverse::client;
    // One client for every project: the project's data picks the host, not the executable.
    const std::string module = projectModule(argc, argv);
#if defined(AIVERSE_CLIENT_ACTOR)
    if (module == "actor") return actorClientMain(argc, argv);
#endif
#if defined(AIVERSE_CLIENT_VOXEL)
    if (module == "voxel") return voxelClientMain(argc, argv);
#endif
#if defined(AIVERSE_SOURCE_DIR)
    useEngineAssets(platform::resolveEngineDir(AIVERSE_SOURCE_DIR, argv[0]));   // the engine's loading screen art in dev runs on a project folder
#endif
    static GameClient client;
    static ClientBoot boot;   // splash / loading screen (ClientSplash.h)
    static size_t autostartMap = size_t(-1);
    g_client = &client;
    platform::AppConfig cfg;
    cfg.title = "AIVERSE";
    cfg.width = 1280; cfg.height = 760;
#ifdef AIVERSE_SPLASH_BACKGROUND   // aiversebuild: game.json splash.background, the window colour before the first frame
    cfg.clearColor[0] = float((AIVERSE_SPLASH_BACKGROUND >> 16) & 0xff) / 255.0f;
    cfg.clearColor[1] = float((AIVERSE_SPLASH_BACKGROUND >> 8) & 0xff) / 255.0f;
    cfg.clearColor[2] = float(AIVERSE_SPLASH_BACKGROUND & 0xff) / 255.0f;
#endif
    platform::AppCallbacks cb;
    cb.frame = [](gfx::Device& d, double dt) { boot.frame(d, dt); };
    cb.event = [](const Event& e) { boot.event(e); };
    cb.shutdown = [] { boot.shutdown(); };
    // --howto: open "How to play"; --map N [--spectate]: jump straight into a match (CI screenshots, quick testing); --lang tag; --size WxH
    std::string previewScreen, previewParams, previewProfile, previewLua;
    static bool sizeFromArgs = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--spectate") g_client->spectate();
        if (a == "--howto") g_client->showHowto();
        // --ui-screen name [--ui-params json] [--ui-profile json] [--ui-lua code]: one screen of the game (MCP ui_preview)
        if (a == "--ui-screen" && i + 1 < argc) previewScreen = argv[++i];
        if (a == "--ui-params" && i + 1 < argc) previewParams = argv[++i];
        if (a == "--ui-profile" && i + 1 < argc) previewProfile = argv[++i];
        if (a == "--ui-lua" && i + 1 < argc) previewLua = argv[++i];
        if (a == "--lang" && i + 1 < argc) g_client->forceLanguage(argv[++i]);
        // --size WxH: window size on desktops (e.g. 540x960 to look at a portrait game)
        if (a == "--size" && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { cfg.width = w; cfg.height = h; sizeFromArgs = true; }
        }
        if (a == "--map" && i + 1 < argc) autostartMap = size_t(std::stoul(argv[++i]));
        // --gfx vulkan|gl: RHI backend on Windows, Linux, Android (game.json "graphics", GraphicsSettings.h)
        if (a == "--gfx" && i + 1 < argc) cfg.graphics = argv[++i];
    }
    if (!previewScreen.empty()) {   // Lua long strings: the JSON goes in verbatim
        auto lng = [](const std::string& t) { return "[==[" + t + "]==]"; };
        std::string code;
        if (!previewProfile.empty()) code += "profile.merge(assert(game.decode(" + lng(previewProfile) + ")))\n";
        code += "ui.preview(" + lng(previewScreen) + ", " + (previewParams.empty() ? std::string("{}") : "assert(game.decode(" + lng(previewParams) + "))") + ")\n";
        code += previewLua;
        g_client->previewScreen(code);
    }
    // AIVERSE_SPLASH=off: load in the init callback (old behaviour); full: the whole splash even with --map
    ClientBoot::Mode mode = autostartMap != size_t(-1) ? ClientBoot::Mode::LoadingOnly : ClientBoot::Mode::Full;
    if (const char* s = std::getenv("AIVERSE_SPLASH")) {
        if (std::string(s) == "off") mode = ClientBoot::Mode::Off;
        else if (std::string(s) == "full") mode = ClientBoot::Mode::Full;
    }
    if (!previewScreen.empty()) mode = ClientBoot::Mode::Off;   // previews show the screen at once
    // game.json "window": size and mode of the desktop window (WindowSettings.h). Screenshot / CI runs
    // (AIVERSE_EXIT_AFTER_FRAMES) and --size stay windowed at the asked size, so captures are the same on every machine.
    cb.configure = [](platform::AppConfig& c) {
        std::string text;
        if (!platform::readContentFile("game.json", text)) return;
        try {
            const Json game = Json::parse(text);
            platform::applyGraphicsSettings(c, game["graphics"], platform::name());
            const Json& window = game["window"];
            const bool capture = std::getenv("AIVERSE_EXIT_AFTER_FRAMES") != nullptr;
            platform::applyWindowSettings(c, window, platform::name(), sizeFromArgs || capture);
            if (capture || sizeFromArgs) c.windowMode = platform::AppConfig::WindowMode::Windowed;
        } catch (const std::exception&) {}   // a broken game.json is reported by the game itself later
    };
    cb.init = [mode](gfx::Device& d) {
        boot.init(d, *g_client, mode, [] { if (autostartMap != size_t(-1)) g_client->autostart(autostartMap); });
    };
    return platform::run(cfg, cb);
}
