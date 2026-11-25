#pragma once
// Presentation-only bounded chart lattice. Does not change gameplay SDF.
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
namespace aiverse::terraweave::caverns {
struct CaveChartPreviewLattice {
    int32_t halfExtentCm=0;
    uint32_t resolution=8;
    int32_t spacingCm=25;
    std::set<ChunkKey3,KeyLess> keys;
};
inline CaveChartPreviewLattice caveChartPreviewLattice(int32_t cap){
    if(cap<1||cap>15000)throw std::invalid_argument("Caverns chart preview extent");
    CaveChartPreviewLattice out;out.halfExtentCm=cap;
    // Two slots per half-axis at most; 200 cm quantum gives exact Q16
    // spans at base resolution and both supported visual LODs.
    out.spacingCm=25*((cap+399)/400);
    const int64_t span=int64_t(out.resolution)*out.spacingCm;
    const int64_t half=(cap+span-1)/span;
    for(int64_t z=-half;z<half;++z)
        for(int64_t y=-half;y<half;++y)
            for(int64_t x=-half;x<half;++x)out.keys.insert({x,y,z});
    return out;
}
inline int64_t caveChartPreviewField(int64_t field,PointCm p,int32_t cap){
    // Intersect positive-inside cave field with the chart box. Padded
    // lattice and normal gradients can sample outside the authored box.
    return std::min({field,int64_t(cap)-std::abs(p.x),
        int64_t(cap)-std::abs(p.y),int64_t(cap)-std::abs(p.z)});
}
inline std::map<ChunkKey3,uint32_t,KeyLess> caveChartPreviewLods(
        const std::set<ChunkKey3,KeyLess>& keys,const std::set<ChunkKey3,KeyLess>& exact,
        ChunkKey3 camera,const std::map<ChunkKey3,uint32_t,KeyLess>& previous){
    CaveDistanceLodSettings settings;settings.maxLod=2;
    settings.transitionChunks={2,4,8,16};settings.hysteresisChunks=1;
    std::map<ChunkKey3,uint32_t,KeyLess> result;
    for(const auto& key:keys){
        const auto prior=previous.find(key);
        const auto distance=uint64_t(std::max({std::abs(key.x-camera.x),
            std::abs(key.y-camera.y),std::abs(key.z-camera.z)}));
        result.emplace(key,exact.contains(key)?0u:caveDistanceLod(distance,
            prior==previous.end()?3u:prior->second,settings));
    }
    return result;
}
} // namespace aiverse::terraweave::caverns
