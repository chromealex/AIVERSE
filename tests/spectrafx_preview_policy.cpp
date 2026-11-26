// Standalone CPU policy tests for the editor's SpectraFX GPU preview.
// No GUI, renderer, Metal, platform event loop or game genre required.
#include <cmath>
#include <cstdio>
#include <limits>
#include "apps/editor/core/SpectraFxGpuPreviewPolicy.h"

using aiverse::editor::SpectraFxGpuPreviewPolicy;
namespace {
int failures=0;
void check(bool ok,const char* why) {
    if(!ok) {std::fprintf(stderr,"FAIL [spectrafx-preview-policy] %s\n",why);++failures;}
}
bool close(float a,float b){return std::fabs(a-b)<1e-6f;}
}
int main() {
    SpectraFxGpuPreviewPolicy p;
    check(p.needsStart(42) && !p.canDraw(42),
          "initial draft requires a Play");
    check(close(p.advance(10.0,true),0.0f),"first frame never catches up");
    check(close(p.advance(10.5,true),0.05f),"frame delta clamps to 50 ms");
    p.didStart(42);
    check(p.canDraw(42) && !p.needsStart(42),"successful revision is live");
    p.togglePause();
    check(p.paused(),"pause toggles on");
    check(close(p.advance(11.0,true),0),"paused frame never advances simulation");
    p.requestStep();
    check(p.paused() && p.stepPending(),"step pauses playback and queues one tick");
    check(close(p.advance(12.0,false),0) && p.stepPending(),
          "invalid or unavailable preview cannot consume Step");
    check(close(p.advance(12.1,true),1.0f/60.0f) && !p.stepPending(),
          "single Step advances exactly 1/60 second");
    check(close(p.advance(13.0,true),0),"step does not auto-repeat");
    p.requestStep();
    p.togglePause();
    check(!p.paused() && !p.stepPending(),
          "Resume cancels stale pending Step");
    check(close(p.advance(13.5,true),0.05f),"resume clamps wall clock delta");
    check(close(p.advance(std::numeric_limits<double>::quiet_NaN(),true),0),
          "nonfinite time source never causes nonfinite delta");
    check(close(p.advance(14.0,true),0),
          "clock recovers cleanly from nonfinite sample");
    p.requestStep();
    p.didFail(84);
    check(!p.canDraw(42) && !p.needsStart(84),
          "failed GPU dispatch cannot be marked live or retried every frame");
    check(close(p.advance(14.1,p.canDraw(84)),0) && p.stepPending(),
          "failed dispatch retains queued Step until a playable source exists");
    check(p.needsStart(85),"editing failed JSON revision enables retry");
    p.requestRestart();
    check(p.needsStart(84),"manual Restart enables same failed revision retry");
    p.didStart(84);
    check(p.canDraw(84),"new successful revision becomes live");
    check(close(p.advance(14.2,p.canDraw(84)),1.0f/60.0f) &&
          !p.stepPending(),
          "retry success consumes exactly one previously queued Step");
    p.invalidDraft();
    check(!p.canDraw(84) && p.needsStart(84),
          "invalid draft clears presentation state and forces fresh Play");
    p.reset();
    check(!p.paused() && !p.stepPending() && p.needsStart(0),
          "project close resets time and revision state, even for hash zero");
    if(failures)return 1;
    std::puts("PASS [spectrafx-preview-policy] pause, step, invalid draft, "
              "revision retry, restart and reset");
    return 0;
}
