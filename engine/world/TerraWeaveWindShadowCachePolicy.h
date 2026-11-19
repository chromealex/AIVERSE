// TerraWeave Horizon: bounded inactive GPU-region wind shadow residency.
// No backend readback is assumed: over-budget cache entries are retired as
// whole models, and re-entry uses the existing async bake/upload pipeline.
#pragma once
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

struct HorizonWindShadowCachePolicy {
    // CPU bytes retained for wind overlays inside INACTIVE GPU cache entries.
    // Excludes active region and outgoing crossfade (both keep their shadows).
    // Zero disables caching regions that would retain a wind CPU shadow.
    uint32_t maxCachedShadowBytes=16u*1024u*1024u;
    // Stage38 opt-in: pack exact dormant vertex bytes losslessly.
    // Re-entry restores the CPU shadow before existing GPU patch planning.
    // Default false preserves Stage37 cache CPU/update characteristics.
    bool compressCachedShadows=false;
};

inline void validateHorizonWindShadowCachePolicy(
        const HorizonWindShadowCachePolicy& policy) {
    if(policy.maxCachedShadowBytes>16u*1024u*1024u)
        throw std::invalid_argument("TerraWeave wind shadow cache budget");
}

} // namespace aiverse::terraweave
