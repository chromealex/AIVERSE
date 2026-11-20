// Explicit, opt-in adapters: one source ID can feed existing LightSystem,
// new LumaWeave direct shading, and the GiDynamicRuntime point-light delta.
// Never render direct light in BOTH LightSystem and LumaWeave simultaneously.
// GI consumers must exclude any source that already contributed to a bake.
#pragma once
#include "LumaWeaveLights.h"
#include "Lights.h"
#include "GiDynamicProbeReference.h"
#include <stdexcept>
#include <vector>

namespace aiverse::render {

inline LumaLight lumaFromLegacy(const PointLight& old,uint64_t stableKey,
                               bool contributesToGi=false) {
    LumaLight out;
    out.key=stableKey; // do NOT invent identity from a per-frame vector index
    out.kind=old.spot()?LumaLightKind::Spot:LumaLightKind::Point;
    out.position=old.pos;
    out.direction=old.dir;
    out.color=old.color;
    out.radius=old.radius;
    out.intensity=old.intensity;
    out.cosInner=old.cosInner;
    out.cosOuter=old.cosOuter;
    out.importanceBias=std::clamp(1.f+old.priority,.01f,64.f);
    out.castsShadows=true;
    out.contributesToGi=contributesToGi;
    if(!lumaValidLight(out))
        throw std::invalid_argument("invalid legacy light for LumaWeave");
    return out;
}

// GiDynamicProbeReference currently models POINT sources only. Failing
// explicitly for an GI-enabled spot avoids silently replacing a cone with an
// omnidirectional bounce. A GI spot/area extension can be added later.
inline std::vector<GiMovingLight> lumaGiPointSources(
        const LumaWeaveRegistry& registry,size_t maxSources=64) {
    if(maxSources>64)throw std::invalid_argument("GI CPU reference source limit is 64");
    std::vector<GiMovingLight> result;
    for(const auto& light:registry.lights()) {
        if(!light.contributesToGi || light.intensity==0)continue;
        if(light.kind!=LumaLightKind::Point)
            throw std::invalid_argument("GI spot light requires a cone-aware transport backend");
        if(result.size()>=maxSources)
            throw std::length_error("GI source budget reached; explicit subset policy required");
        result.push_back({light.key,light.position,light.color,light.radius,light.intensity});
    }
    return result;
}

} // namespace aiverse::render
