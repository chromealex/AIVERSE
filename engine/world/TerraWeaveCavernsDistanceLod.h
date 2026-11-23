#pragma once
// Stage37: deterministic per-chunk visual cave LOD from camera distance.
// This policy affects PRESENTATION ONLY: Box3D/SpatialWorld use full-res
// authored collision geometry independent of camera and visual LOD.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
struct CaveDistanceLodSettings {
    std::array<uint32_t,4> transitionChunks{2,4,8,16};
    uint32_t maxLod=3;
    uint32_t hysteresisChunks=1;
};
inline void validateCaveDistanceLod(CaveDistanceLodSettings c){
    if(c.maxLod>4||c.hysteresisChunks>4)
        throw std::invalid_argument("Caverns distance LOD limits");
    for(size_t i=0;i<c.transitionChunks.size();++i)
        if(!c.transitionChunks[i]||c.transitionChunks[i]>128||
           (i && c.transitionChunks[i]<=c.transitionChunks[i-1]))
            throw std::invalid_argument("Caverns distance LOD cutoffs");
}
inline uint32_t caveDistanceLod(
    uint64_t distanceChunks,uint32_t prior,
    CaveDistanceLodSettings settings={}) {
    validateCaveDistanceLod(settings);
    uint32_t desired=0;
    while(desired<settings.maxLod &&
          distanceChunks>=settings.transitionChunks[desired])
        ++desired;
    if(prior>settings.maxLod)return desired;
    if(desired>prior) {
        const uint64_t downThreshold=
            uint64_t(settings.transitionChunks[prior])+
            settings.hysteresisChunks;
        if(distanceChunks<downThreshold)return prior;
    } else if(desired<prior) {
        const uint64_t upThreshold=
            uint64_t(settings.transitionChunks[desired])+
            settings.hysteresisChunks;
        if(distanceChunks>=upThreshold)return prior;
    }
    return desired;
}
struct CaveVisualLodMeshSpec {
    uint32_t resolution=0;
    int32_t spacingCm=0;
};
inline CaveVisualLodMeshSpec caveVisualLodMeshSpec(
    uint32_t baseResolution,int32_t baseSpacingCm,uint32_t lod) {
    if(!baseResolution||baseResolution>64||baseSpacingCm<=0||
       baseSpacingCm>10000||lod>4||baseResolution%(1u<<lod)||
       int64_t(baseSpacingCm)*(1u<<lod)>10000)
        throw std::invalid_argument("Caverns visual LOD mesh dimensions");
    return {baseResolution>>lod,
            int32_t(int64_t(baseSpacingCm)*(1u<<lod))};
}
} // namespace aiverse::terraweave::caverns
