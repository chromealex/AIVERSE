// Client startup with the splash / loading screen (game.json "splash", engine/splash). The first
// frame only reads game.json and draws the splash; every following frame runs one GameClient init
// step (rules, renderer, view models + textures, effects, audio, UI) while the splash animates and
// its progress bar follows the real work (plus pack chunk downloads in split builds with
// "waitForContent"). When loading is done and the minimum time has passed (or the player tapped a
// skippable splash), the menu fades in under the splash.
//
// What is shown is the project's choice (game.json "splash", splash::resolveBlock: engine screen, changed,
// own or none). AIVERSE_SPLASH=off loads everything in the init callback as before; with --map (tests, CI
// screenshots) the splash only covers the loading time unless AIVERSE_SPLASH=full.
#pragma once
#include <functional>

#include "GameClient.h"
#include "engine/splash/SplashBoot.h"

namespace aiverse::client {

class ClientBoot {
public:
    enum class Mode { Full, LoadingOnly, Off };
    void init(gfx::Device& dev, GameClient& client, Mode mode, std::function<void()> afterLoad);
    void frame(gfx::Device& dev, double dt);
    void event(const platform::Event& e);
    void shutdown();
    bool booting() const { return boot_.booting(); }

private:
    GameClient* client_ = nullptr;
    splash::Boot boot_;   // the shared startup (engine/splash/SplashBoot.h)
};

} // namespace aiverse::client
