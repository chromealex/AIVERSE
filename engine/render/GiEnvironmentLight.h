// Optional *indirect-only* sun and sky inputs for moving-probe first bounce.
// These are not the direct sun/hemisphere terms in Lighting or a baked GI base.
// Default zero energy prevents double counting previously baked environmental light.
#pragma once
#include "Math3D.h"
#include <cmath>

namespace aiverse::render {

struct GiEnvironmentLight {
    // World-space direction FROM receiver TOWARD the sun (same as Lighting::sunDir).
    float3 sunDirection{0,1,0};
    float3 sunColor{1,1,1};
    float sunIntensity=0;
    // Cheap directional-overhead sky visibility: the sky is sampled along +Y.
    // This is a deliberately bounded one-ray approximation, not multi-bounce sky RT.
    float3 skyColor{1,1,1};
    float skyIntensity=0;

    bool enabled() const { return sunIntensity>0 || skyIntensity>0; }
    bool valid() const {
        const auto color=[](float3 c) {
            return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) &&
                   c.x>=0 && c.y>=0 && c.z>=0 &&
                   c.x<=10000 && c.y<=10000 && c.z<=10000;
        };
        return color(sunColor) && color(skyColor) &&
               std::isfinite(sunIntensity) && sunIntensity>=0 && sunIntensity<=10000 &&
               std::isfinite(skyIntensity) && skyIntensity>=0 && skyIntensity<=10000 &&
               std::isfinite(sunDirection.x) && std::isfinite(sunDirection.y) &&
               std::isfinite(sunDirection.z) &&
               std::abs(dot(sunDirection,sunDirection)-1.0f) < .002f;
    }
    bool operator==(const GiEnvironmentLight& b) const {
        return sunDirection.x==b.sunDirection.x &&
               sunDirection.y==b.sunDirection.y &&
               sunDirection.z==b.sunDirection.z &&
               sunColor.x==b.sunColor.x && sunColor.y==b.sunColor.y &&
               sunColor.z==b.sunColor.z && sunIntensity==b.sunIntensity &&
               skyColor.x==b.skyColor.x && skyColor.y==b.skyColor.y &&
               skyColor.z==b.skyColor.z && skyIntensity==b.skyIntensity;
    }
};

} // namespace aiverse::render
