// SpectraFX native editor's pure presentation-control policy.
// No ImGui, RHI, game module, ECS or platform dependencies. The host
// owns actual GPU resources and handles rollback at a safe RHI boundary.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aiverse::editor {

class SpectraFxGpuPreviewPolicy {
public:
    void reset() { *this=SpectraFxGpuPreviewPolicy{}; }

    bool paused() const { return paused_; }
    void togglePause() {
        paused_=!paused_;
        if(!paused_) stepPending_=false;
    }
    void requestStep() {
        paused_=true;
        stepPending_=true;
    }
    bool stepPending() const { return stepPending_; }

    // Sample once per Editor UI frame, including invalid or failed drafts.
    // Updating the clock while inactive prevents catch-up after recovery.
    // A requested step is preserved until a valid, initialized preview
    // can actually consume it.
    float advance(double now,bool previewReady) {
        double elapsed=0.0;
        if(std::isfinite(now) && std::isfinite(lastTime_) &&
           now>=lastTime_)
            elapsed=now-lastTime_;
        lastTime_=std::isfinite(now)?now:
                  std::numeric_limits<double>::quiet_NaN();
        if(!previewReady) return 0.0f;
        if(stepPending_) {
            stepPending_=false;
            return 1.0f/60.0f;
        }
        if(paused_) return 0.0f;
        return float(std::clamp(elapsed,0.0,0.05));
    }

    // GPU asset text may fail to dispatch even when JSON parsing succeeded
    // (no compatible backend, missing CPU asset, device failure). Never
    // misreport that as "applied". Do not repeat failed resets every frame.
    bool needsStart(uint64_t hash) const {
        if(restartRequested_) return true;
        if(applied_ && appliedHash_==hash) return false;
        return !failed_ || failedHash_!=hash;
    }
    bool canDraw(uint64_t hash) const {
        return applied_ && appliedHash_==hash && !restartRequested_;
    }
    void requestRestart() {
        restartRequested_=true;
        failed_=false;
    }
    void didStart(uint64_t hash) {
        applied_=true;appliedHash_=hash;
        failed_=false;restartRequested_=false;
    }
    void didFail(uint64_t hash) {
        applied_=false;
        failed_=true;failedHash_=hash;
        restartRequested_=false;
    }
    // Invalid unsaved JSON must hide the previous generation. Once a
    // valid revision arrives it must be eligible for a new Play.
    void invalidDraft() {
        applied_=false;
        failed_=false;
        restartRequested_=true;
    }

private:
    double lastTime_=std::numeric_limits<double>::quiet_NaN();
    uint64_t appliedHash_=0,failedHash_=0;
    bool applied_=false,failed_=false,restartRequested_=true;
    bool paused_=false,stepPending_=false;
};

} // namespace aiverse::editor
