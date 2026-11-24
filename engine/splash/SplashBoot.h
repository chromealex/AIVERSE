// Client startup behind the loading screen, shared by every host client (game, actor, voxel): the first
// frame only draws the screen, then one load step runs per frame while it animates and its bar follows
// the real work; once loading is done and the minimum time has passed (or the player skipped), the game
// fades in under it. What is shown comes from game.json "splash" through splash::resolveBlock - a project
// can keep the engine screen, change parts of it, replace it or switch it off ("splash": false).
#pragma once
#include <functional>
#include <string>

#include "engine/splash/SplashScreen.h"

namespace aiverse::splash {

class Boot {
public:
    // Full: the whole screen (minimum time, skippable). LoadingOnly: leave as soon as loading is done (tests,
    // screenshots, --map runs). Off: no screen, every step runs inside init (AIVERSE_SPLASH=off).
    enum class Mode { Full, LoadingOnly, Off };
    struct Hooks {
        int steps = 1;                                                  // load steps, one per frame after the first
        std::function<void(gfx::Device&, int)> step;                    // runs load step i
        std::function<float(int)> weight;                               // share of step i in the bar (default: equal)
        std::function<void(gfx::Device&, double)> frame;                // the game's own frame once loaded
        std::function<void(gfx::Device&, double, float, int)> overlay;  // optional extra drawing while loading (progress, step)
        std::function<bool()> moreLoading;                              // optional: loading still runs after the steps
        std::function<float()> moreProgress;                            // its progress 0..1 (shares the bar half/half)
        std::function<void()> afterLoad;                                // once, when loading is done
        std::function<bool()> handOver;                                 // after afterLoad: true = the game shows its own loading next (no fade)
        std::function<float()> pixelScale;                              // dpi scale for fit "center" layers (default 1)
    };
    // `splash` is the game.json "splash" value (resolved here); `read` loads its images.
    void init(gfx::Device& dev, const Json& splash, Mode mode, SplashScreen::ReadFn read, Hooks hooks);
    void frame(gfx::Device& dev, double dt);
    void skip() { if (phase_ == Phase::Loading) screen_.skip(); }   // tap / click / key
    void shutdown() { screen_.shutdown(); }
    bool booting() const { return phase_ != Phase::Done; }
    bool loading() const { return phase_ == Phase::Loading; }   // events go to the game once this is false (fade included)
    bool loadStarted() const { return step_ > 0; }          // some load step already ran
    bool loaded() const { return step_ >= hooks_.steps; }
    bool off() const { return off_; }
    const std::string& error() const { return error_; }

private:
    enum class Phase { Loading, Fading, Done };
    float progress() const;
    void finish(gfx::Device& dev, double dt);
    Hooks hooks_;
    SplashScreen screen_;
    Mode mode_ = Mode::Full;
    Phase phase_ = Phase::Loading;
    int step_ = -1;   // -1: the first frame only draws
    float fade_ = 1;
    bool off_ = false;
    std::string error_;
};

} // namespace aiverse::splash
