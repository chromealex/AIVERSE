// PHYSIS wildfire/wind -> TerraWeave grass presentation (2026-11-21).
// Host-level glue: maps a NaturePresentationFrame onto the optional
// terraweave::GrassPresentationOptions overlay. TerraWeave keeps grass
// coverage ownership; PHYSIS keeps fire/fuel ownership; nothing is written
// back. Charred blades persist because PHYSIS fuel persists.
#pragma once
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/render/TerraWeaveGrassRender.h"
#include <cmath>
#include <memory>

namespace aiverse::world {

// Scene/PHYSIS metres = (TerraWeave cm - originCm) / 100 + offsetMetres.
struct GrassPhysisMapping {
    int64_t originXcm = 0, originZcm = 0;
    float offsetXm = 0, offsetZm = 0;
    float metresX(int64_t xcm) const { return float(xcm - originXcm) * 0.01f + offsetXm; }
    float metresZ(int64_t zcm) const { return float(zcm - originZcm) * 0.01f + offsetZm; }
};

inline terraweave::GrassFireSample grassFireSample(const NaturePresentationFrame& frame,
                                                   float xm, float zm) {
    terraweave::GrassFireSample out;
    const auto s = frame.sample(xm, zm);
    if (!s.valid) return out;
    out.charPermille = uint16_t(std::lround(std::clamp(s.charred, 0.f, 1.f) * 1000.f));
    out.firePermille = uint16_t(std::lround(std::clamp(s.fire, 0.f, 1.f) * 1000.f));
    return out;
}

// Installs the overlay on a GrassPatchRenderer. Keep `frame` alive (shared)
// until the next call; call again with each new PHYSIS tick's frame.
inline void applyPhysisGrassPresentation(terraweave::GrassPatchRenderer& grass,
                                         std::shared_ptr<const NaturePresentationFrame> frame,
                                         GrassPhysisMapping map = {}, bool windSway = true) {
    if (!frame || !frame->valid || !frame->hasDisasters) {
        terraweave::GrassPresentationOptions o;
        o.windSway = windSway;
        grass.setPresentation(o, {});
        return;
    }
    terraweave::GrassPresentationOptions o;
    o.windSway = windSway;
    o.fire = [frame, map](int64_t x, int64_t z) {
        return grassFireSample(*frame, map.metresX(x), map.metresZ(z));
    };
    grass.setPresentation(std::move(o), [frame, map](const terraweave::RectCm& r) {
        return frame->signature(map.metresX(r.minX), map.metresZ(r.minZ),
                                map.metresX(r.maxX), map.metresZ(r.maxZ));
    });
}

// Per render frame: local wind for the sway shader (phase advances with
// render time; gust from local speed variance is approximated by speed).
inline void applyPhysisGrassWind(terraweave::GrassPatchRenderer& grass,
                                 const NaturePresentationFrame& frame,
                                 float xm, float zm, double seconds) {
    const auto s = frame.sample(xm, zm);
    const float wx = s.valid ? s.windX : frame.meanWindX;
    const float wz = s.valid ? s.windZ : frame.meanWindZ;
    const float speed = std::sqrt(wx * wx + wz * wz);
    const float phase = float(std::fmod(seconds * (1.4 + 0.12 * speed), 6.283185307179586));
    grass.setWind(wx, wz, phase, std::clamp(speed / 25.f, 0.f, 1.f));
}

} // namespace aiverse::world
