// Explicit bridge from Renderer3D's direct sun/sky scene settings to the
// *indirect-only* dynamic GI environment. Source values are captured after
// scene authoring. Multipliers default to zero to prevent baked GI double count.
#pragma once
#include "GiEnvironmentLight.h"
#include "Renderer3D.h"
#include <cmath>
#include <stdexcept>

namespace aiverse::render {

inline GiEnvironmentLight giEnvironmentFromLighting(
    const Lighting& direct, float sunBounceMultiplier=0,
    float skyBounceMultiplier=0) {
    if(!std::isfinite(sunBounceMultiplier) || sunBounceMultiplier<0 ||
       !std::isfinite(skyBounceMultiplier) || skyBounceMultiplier<0 ||
       !std::isfinite(direct.sunIntensity) || direct.sunIntensity<0)
        throw std::invalid_argument("invalid GI environmental source multiplier");

    GiEnvironmentLight gi;
    // Ignore unrelated weather/shadow/tonemap parameters; this is only
    // source radiance, not another direct-light pass.
    gi.sunColor=direct.sunColor;
    gi.skyColor=direct.skyColor;
    gi.sunIntensity=direct.sunIntensity*sunBounceMultiplier;
    gi.skyIntensity=skyBounceMultiplier; // skyColor itself holds the radiance
    if(gi.sunIntensity>0) {
        const float d2=dot(direct.sunDir,direct.sunDir);
        if(!std::isfinite(d2) || d2<=1e-8f)
            throw std::invalid_argument("invalid sun direction for indirect GI");
        gi.sunDirection=normalize(direct.sunDir);
    }
    if(!gi.valid())throw std::invalid_argument("invalid GI environment radiance");
    return gi;
}

} // namespace aiverse::render
