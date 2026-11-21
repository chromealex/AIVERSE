#pragma once
#include <algorithm>
#include <cmath>
namespace aiverse::render {
// Per-view presentation policy. No GPU ownership: callers allocate only on a
// reported change. A rejected allocation is not retried every frame.
struct AdaptiveShadowResolution {
    int resolution=0, quietFrames=0, cooldown=0;
    bool update(float requiredTexels, int budget=4096) {
        if(!std::isfinite(requiredTexels)||requiredTexels<0)return false;
        const int cap=budget>=4096?4096:budget>=2048?2048:1024;
        int desired=requiredTexels>2048?4096:requiredTexels>1024?2048:1024;
        desired=std::min(desired,cap);
        if(!resolution || resolution>cap) {
            resolution=desired;quietFrames=0;cooldown=120;return true;
        }
        if(cooldown>0)--cooldown;
        if(desired>resolution && requiredTexels>resolution*1.15f) {
            resolution=desired;quietFrames=0;cooldown=120;return true;
        }
        if(desired<resolution && requiredTexels<resolution*.40f) {
            if(++quietFrames>=60 && cooldown==0) {
                resolution=desired;quietFrames=0;cooldown=120;return true;
            }
        } else quietFrames=0;
        return false;
    }
};
// Estimate receiver density at the camera focus plane. Texture dimensions do
// not enter: shadow depth is projected geometry, not the surface's UV image.
inline float shadowTexelDemand(float worldCoverage,float verticalPixels,
                               float focusDistance,float fovY) {
    if(!(worldCoverage>0&&verticalPixels>0&&focusDistance>0&&fovY>0&&fovY<3.13f))return 0;
    return worldCoverage*verticalPixels/(2*focusDistance*std::tan(fovY*.5f));
}
}
