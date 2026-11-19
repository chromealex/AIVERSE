// TerraWeave Horizon runtime presentation/memory presets.
// Independent of HorizonSettings geometry quality and the TWH8 bake archive.
// Native game projects may apply a complete policy and override individual
// properties afterward without any genre, PHYSIS or renderer backend includes.
#pragma once
#include "engine/world/TerraWeaveWindUploadPolicy.h"
#include "engine/world/TerraWeaveWindShadowCachePolicy.h"
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

enum class HorizonPresentationQualityPreset : uint8_t {
    BatterySaver, Mobile, Balanced, Desktop
};

struct HorizonPresentationPolicy {
    bool exactSpatialWindField=true;
    bool spatialWindVariation=true;
    HorizonWindUploadPolicy windUpload{};
    HorizonWindShadowCachePolicy windShadowCache{};
};

inline void validateHorizonPresentationPolicy(
        const HorizonPresentationPolicy& policy) {
    validateHorizonWindUploadPolicy(policy.windUpload);
    validateHorizonWindShadowCachePolicy(policy.windShadowCache);
}

// These are conservative starting points, NOT measured FPS or memory promises.
// A manifest without presentationQuality keeps all previous runtime defaults.
inline HorizonPresentationPolicy horizonPresentationPolicyForQuality(
        HorizonPresentationQualityPreset preset) {
    HorizonPresentationPolicy p;
    switch(preset) {
        case HorizonPresentationQualityPreset::BatterySaver:
            p.exactSpatialWindField=false;
            p.spatialWindVariation=false;
            p.windUpload.maxBytesPerUpdate=0;
            p.windUpload.maxSparseRanges=1;
            p.windShadowCache.maxCachedShadowBytes=0;
            break;
        case HorizonPresentationQualityPreset::Mobile:
            p.exactSpatialWindField=false;
            p.spatialWindVariation=true;
            p.windUpload.maxBytesPerUpdate=128u*1024u;
            p.windUpload.maxSparseRanges=2;
            p.windUpload.fullFallbackPercent=90;
            p.windShadowCache.maxCachedShadowBytes=1024u*1024u;
            p.windShadowCache.compressCachedShadows=true;
            break;
        case HorizonPresentationQualityPreset::Balanced:
            p.windUpload.maxBytesPerUpdate=512u*1024u;
            p.windUpload.maxSparseRanges=4;
            p.windUpload.fullFallbackPercent=75;
            p.windShadowCache.maxCachedShadowBytes=4u*1024u*1024u;
            p.windShadowCache.compressCachedShadows=true;
            break;
        case HorizonPresentationQualityPreset::Desktop:
            p.windUpload.maxBytesPerUpdate=2u*1024u*1024u;
            p.windUpload.maxSparseRanges=8;
            p.windUpload.fullFallbackPercent=70;
            p.windShadowCache.maxCachedShadowBytes=16u*1024u*1024u;
            p.windShadowCache.compressCachedShadows=true;
            break;
        default:
            throw std::invalid_argument(
                "TerraWeave Horizon presentation quality preset");
    }
    validateHorizonPresentationPolicy(p);
    return p;
}
} // namespace aiverse::terraweave
