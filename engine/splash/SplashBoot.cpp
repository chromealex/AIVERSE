#include "engine/splash/SplashBoot.h"

#include <cstdio>
#include <stdexcept>

namespace aiverse::splash {

void Boot::init(gfx::Device& dev, const Json& splash, Mode mode, SplashScreen::ReadFn read, Hooks hooks) {
    hooks_ = std::move(hooks);
    mode_ = mode;
    Choice choice;
    try { choice = resolveBlock(splash); }
    catch (const std::exception& e) { error_ = e.what(); choice.block = defaultBlock(); }   // a broken block still loads the game
    if (!error_.empty()) std::fprintf(stderr, "%s\n", error_.c_str());
    off_ = choice.off;
    if (mode_ == Mode::Off || off_) {
        for (step_ = 0; step_ < hooks_.steps; ++step_) if (hooks_.step) hooks_.step(dev, step_);
        if (hooks_.afterLoad) hooks_.afterLoad();
        phase_ = Phase::Done;
        return;
    }
    screen_.init(dev, choice.block, std::move(read));
    if (!screen_.error().empty()) std::fprintf(stderr, "splash: %s\n", screen_.error().c_str());
}

float Boot::progress() const {
    float total = 0, done = 0;
    for (int i = 0; i < hooks_.steps; ++i) {
        const float w = hooks_.weight ? hooks_.weight(i) : 1.0f;
        total += w;
        if (i < step_) done += w;
    }
    float p = total > 0 ? done / total : 1.0f;
    if (hooks_.moreProgress) p = p * 0.5f + hooks_.moreProgress() * 0.5f;
    return p;
}

void Boot::finish(gfx::Device& dev, double dt) {
    for (auto& w : screen_.warnings()) std::fprintf(stderr, "%s\n", w.c_str());
    if (hooks_.afterLoad) hooks_.afterLoad();
    if (hooks_.handOver && hooks_.handOver()) {
        phase_ = Phase::Done;
        screen_.shutdown();
        if (hooks_.frame) hooks_.frame(dev, 0);
        return;
    }
    phase_ = Phase::Fading;
    fade_ = 1;
    if (hooks_.frame) hooks_.frame(dev, dt);
    screen_.draw(dev, fade_, false, hooks_.pixelScale ? hooks_.pixelScale() : 1.0f);
}

void Boot::frame(gfx::Device& dev, double dt) {
    if (phase_ == Phase::Done) { if (hooks_.frame) hooks_.frame(dev, dt); return; }
    const float px = hooks_.pixelScale ? hooks_.pixelScale() : 1.0f;
    if (phase_ == Phase::Fading) {
        if (hooks_.frame) hooks_.frame(dev, dt);
        const float len = screen_.spec().fadeOut;
        fade_ = len > 0 ? fade_ - float(dt) / len : 0.0f;
        screen_.update(dt, false);
        if (fade_ <= 0) { phase_ = Phase::Done; screen_.shutdown(); return; }
        screen_.draw(dev, fade_, false, px);
        return;
    }
    // Loading: the first frame only shows the screen; then one load step per frame.
    const bool stepsDone = step_ >= hooks_.steps;
    const bool done = stepsDone && !(hooks_.moreLoading && hooks_.moreLoading());
    screen_.update(dt, !done);
    if (step_ >= 0 && !stepsDone && hooks_.step) hooks_.step(dev, step_);
    if (!stepsDone) ++step_;
    screen_.setProgress(progress());
    const bool leave = mode_ == Mode::LoadingOnly ? done && screen_.imagesLoaded() : screen_.readyToLeave(done);
    if (leave) { finish(dev, dt); return; }
    screen_.draw(dev, 1, true, px);
    if (hooks_.overlay) hooks_.overlay(dev, dt, screen_.shownProgress(), step_);
}

} // namespace aiverse::splash
