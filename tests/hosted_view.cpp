// platform::HostedView (the editor hosting the game client, apps/editor/ClientHost.h): while set, window /
// content queries answer for the hosted view, requestQuit is recorded, "engine/..." maps to the engine
// assets; clearing it restores the app's. Its own executable: the platform layer (rts_runtime) owns main().
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "engine/platform/Platform.h"

using namespace rts;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failed; } } while (0)

int rtsMain(int, char**) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "rts_hosted_view_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "game", ec);
    fs::create_directories(root / "assets" / "ui", ec);
    { std::ofstream(root / "game" / "game.json") << "{\"name\": \"hosted\"}"; }
    { std::ofstream(root / "assets" / "ui" / "x.rml") << "engine asset"; }
    const float w0 = platform::windowWidth();
    const int fb0 = platform::framebufferWidth();
    platform::HostedView v;
    v.width = 375; v.height = 667; v.fbWidth = 750; v.fbHeight = 1334; v.scale = 2; v.touch = true;
    v.safe.top = 20;
    v.contentRoot = (root / "game").string();
    v.engineAssets = (root / "assets").string();
    platform::setHostedView(&v);
    CHECK(platform::windowWidth() == 375 && platform::windowHeight() == 667);
    CHECK(platform::framebufferWidth() == 750 && platform::framebufferHeight() == 1334 && platform::dpiScale() == 2);
    CHECK(platform::isTouchDevice() && platform::safeAreaInsets().top == 20 && !platform::isFullscreen());
    CHECK(platform::contentRoot() == v.contentRoot);
    std::string text;
    CHECK(platform::readContentFile("game.json", text) && text.find("hosted") != std::string::npos);
    CHECK(platform::readContentFile("engine/ui/x.rml", text) && text == "engine asset");
    platform::requestQuit();
    CHECK(v.quitRequested);
    platform::setHostedView(nullptr);
    CHECK(platform::hostedView() == nullptr && platform::windowWidth() == w0 && platform::framebufferWidth() == fb0);
    CHECK(!platform::readContentFile("engine/ui/x.rml", text));
    fs::remove_all(root, ec);
    std::printf(g_failed ? "hosted view: %d FAILED\n" : "hosted view: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
