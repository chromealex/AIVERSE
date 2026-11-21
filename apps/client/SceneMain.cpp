// Minimal game client for projects without a game module (game.json "module": "scene"):
// window / canvas, content pack, and an empty scene cleared to game.json "background"
// ("#rrggbb", default dark grey). It links no simulation, renderer, UI, scripting or audio, so an
// empty project ships only the platform layer and one graphics backend (aiversebuild picks this
// target from game.json; docs/overview.md "Engine modules").
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/data/Json.h"
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"

using namespace aiverse;

namespace {
float g_background[3] = {0.08f, 0.09f, 0.11f};

// "#rrggbb" -> 0..1 RGB; anything else keeps the default.
void parseBackground(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str() + 1, &end, 16);
    if (!end || *end) return;
    g_background[0] = float((v >> 16) & 0xff) / 255.0f;
    g_background[1] = float((v >> 8) & 0xff) / 255.0f;
    g_background[2] = float(v & 0xff) / 255.0f;
}

void readManifest(platform::AppConfig& c) {
    std::string text;
    if (!platform::readContentFile("game.json", text)) {
        std::fprintf(stderr, "scene client: game.json not found in content\n");
        return;
    }
    Json game;
    try {
        game = Json::parse(text);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene client: game.json: %s\n", e.what());
        return;
    }
    c.title = game.getStr("name", c.title);
    parseBackground(game.getStr("background", ""));
    for (int i = 0; i < 3; ++i) c.clearColor[i] = g_background[i];
}
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE";
    cfg.width = 1280;
    cfg.height = 760;
    platform::AppCallbacks cb;
    cb.configure = readManifest;
    cb.init = [](gfx::Device&) {
        // Web and mobile skip configure(): read the manifest once the content is mounted.
        platform::AppConfig c;
        readManifest(c);
        platform::setTitle(c.title);
    };
    cb.frame = [](gfx::Device& d, double) {
        gfx::PassDesc pass;
        for (int i = 0; i < 3; ++i) pass.clearColor[i] = g_background[i];
        pass.clearColor[3] = 1.0f;
        d.beginPass(pass);
        d.endPass();
    };
    return platform::run(cfg, cb);
}
