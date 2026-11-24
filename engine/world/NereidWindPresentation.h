// NEREID visual wind from an external weather/PHYSIS owner.
// The host supplies an immutable sampled wind velocity each frame. This
// presentation-only struct has no dependency on PHYSIS internals or genres.
#pragma once
#include "engine/render/Math3D.h"
#include "engine/world/nereid/NereidWater.h"
#include <algorithm>
#include <cmath>

namespace aiverse::world {

struct NereidVisualWind {
    float east=0.f;      // horizontal world X velocity (m/s)
    float north=0.f;     // horizontal world Z velocity (m/s)
    float gust=0.f;      // 0..1 strength of momentary gusts
    float coupling=1.f;  // 0..1 water response to external wind
    render::float4 shaderUniform()const {
        return {east,north,gust,coupling};
    }
};
inline NereidVisualWind normalizeNereidWind(NereidVisualWind in) {
    auto limit=[](float v,float low,float high) {
        return std::isfinite(v)?std::clamp(v,low,high):low;
    };
    in.east=limit(in.east,-100.f,100.f);
    in.north=limit(in.north,-100.f,100.f);
    in.gust=limit(in.gust,0.f,1.f);
    in.coupling=limit(in.coupling,0.f,1.f);
    return in;
}
// Adapter for PHYSIS and any other weather/physics implementation which
// produces a deterministic Fixed Vec3 wind and gust. Rendering converts
// to float only after the host's ECS read barrier.
inline NereidVisualWind nereidVisualWindFromSample(
        Vec3 worldWind,Fixed gust,Fixed coupling=1_fx) {
    return normalizeNereidWind({
        float(worldWind.x.toDouble()),
        float(worldWind.z.toDouble()),
        float(gust.toDouble()),
        float(coupling.toDouble())});
}
} // namespace aiverse::world
