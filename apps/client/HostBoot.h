// The shared loading screen for host clients that load in one call (actor and voxel projects; the game
// client uses ClientBoot with its own steps). Same rules as every client: game.json "splash" decides
// (splash::resolveBlock - engine screen, changed, own or none), AIVERSE_SPLASH=off|full, and screenshot /
// CI runs (AIVERSE_EXIT_AFTER_FRAMES) leave as soon as loading is done.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>

#include "engine/platform/Platform.h"
#include "engine/splash/SplashBoot.h"

namespace aiverse::client {

inline splash::Boot::Mode hostBootMode() {
    if (const char* s = std::getenv("AIVERSE_SPLASH")) {
        if (std::string(s) == "off") return splash::Boot::Mode::Off;
        if (std::string(s) == "full") return splash::Boot::Mode::Full;
    }
    return std::getenv("AIVERSE_EXIT_AFTER_FRAMES") ? splash::Boot::Mode::LoadingOnly : splash::Boot::Mode::Full;
}

// Engine assets for "engine/x" content paths when the client runs straight on a project folder.
// Only a fallback: files under the content root (a package's content/engine) still win.
inline void useEngineAssets(const std::string& engineRoot) {
    std::error_code ec;
    if (platform::engineAssetsDir().empty() && std::filesystem::is_directory(engineRoot + "/engine/assets/branding", ec))
        platform::setEngineAssetsDir(engineRoot + "/engine/assets");
}

// Wires an app with init(dev) / frame(dev, dt) / event(e) / shutdown(dev) into the platform callbacks:
// the first frames show the loading screen, the next one runs init, then the game fades in under it.
struct HostBoot {
    splash::Boot boot;
    bool started = false;
    gfx::Device* device = nullptr;
    template<class App> void install(platform::AppCallbacks& cb, App& app, const Json& splashValue, const std::string& project) {
        cb.init = [this, &app, splashValue, project](gfx::Device& d) {
            device = &d;
            splash::Boot::Hooks h;
            h.steps = 1;
            h.step = [this, &app](gfx::Device& dev, int) { app.init(dev); started = true; };
            h.frame = [&app](gfx::Device& dev, double dt) { app.frame(dev, dt); };
            h.pixelScale = [] { return platform::dpiScale(); };
            // Project files are relative to the project folder; "engine/x" is the engine's own art.
            auto read = [project](const std::string& path, std::string& out) {
                if (path.rfind("engine/", 0) == 0 && platform::readContentFile(path, out)) return true;
                return platform::readContentFile(project + "/" + path, out);
            };
            boot.init(d, splashValue, hostBootMode(), read, std::move(h));
        };
        cb.frame = [this](gfx::Device& d, double dt) { boot.frame(d, dt); };
        cb.event = [this, &app](const platform::Event& e) {
            if (!boot.loading()) { app.event(e); return; }
            using E = platform::EventType;
            if (e.type == E::MouseDown || e.type == E::TouchBegin || (e.type == E::KeyDown && !e.repeat)) boot.skip();
        };
        cb.shutdown = [this, &app] { boot.shutdown(); if (started) app.shutdown(*device); };
    }
};

} // namespace aiverse::client
