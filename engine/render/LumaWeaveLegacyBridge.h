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

// The current GiMovingLight CPU reference supports explicit spot cones.
// Preserve authored point/spot direction instead of silently treating spots
// as omni sources or dropping their indirect contribution.
inline std::vector<GiMovingLight> lumaGiMovingSources(
        const LumaWeaveRegistry& registry,size_t maxSources=64) {
    if(maxSources>64)throw std::invalid_argument("GI CPU reference source limit is 64");
    std::vector<GiMovingLight> result;
    for(const auto& light:registry.lights()) {
        if(!light.contributesToGi || light.intensity==0)continue;
        if(result.size()>=maxSources)
            throw std::length_error("GI source budget reached; explicit subset policy required");
        GiMovingLight gi;
        gi.key=light.key;
        gi.position=light.position;
        gi.color=light.color;
        gi.radius=light.radius;
        gi.intensity=light.intensity;
        if(light.kind==LumaLightKind::Spot) {
            gi.direction=light.direction;
            gi.cosInner=light.cosInner;
            gi.cosOuter=light.cosOuter;
        }
        result.push_back(gi);
    }
    return result;
}
// Historical name kept for source compatibility. It now preserves spot
// cones because the actual CPU GI transport supports them.
inline std::vector<GiMovingLight> lumaGiPointSources(
        const LumaWeaveRegistry& registry,size_t maxSources=64) {
    return lumaGiMovingSources(registry,maxSources);
}

} // namespace aiverse::render
