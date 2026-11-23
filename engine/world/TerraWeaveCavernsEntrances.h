#pragma once
// Pure opt-in surface entrances. Never mutates TerraWeave heightfield.
#include "TerraWeaveCavernsMesh.h"
#include <optional>
#include <limits>
namespace aiverse::terraweave::caverns {
struct SurfaceRectCm {int64_t minX=0,minZ=0,maxX=0,maxZ=0;};
struct EntranceSettings {
    uint64_t seed=0x5eeda3ULL;
    int32_t spacingCm=8000;
    int32_t radiusCm=450;
    int32_t minDepthCm=500;
    int32_t maxDepthCm=16000;
    int32_t depthStepCm=100;
    uint32_t chancePermille=350;
};
struct Entrance {
    uint64_t id=0;
    int64_t xCm=0, zCm=0;
    int64_t surfaceYcm=0, caveYcm=0;
    int32_t radiusCm=0;
    constexpr bool operator==(const Entrance&) const = default;
};
inline void validateEntrances(const EntranceSettings& e) {
    if(e.spacingCm<100 || e.spacingCm>1000000 ||
       e.radiusCm<50 || int64_t(e.radiusCm)*4>e.spacingCm ||
       e.minDepthCm<1 || e.maxDepthCm<e.minDepthCm ||
       e.maxDepthCm>1000000 || e.depthStepCm<1 ||
       e.chancePermille>1000)
        throw std::invalid_argument("TerraWeave Caverns entrance settings");
}
// Surface sampler: int64_t heightAtCm(int64_t x, int64_t z).
// Exclusion predicate: bool excluded(int64_t x, int64_t z), e.g. authored buildings.
template<class HeightAt,class Excluded>
std::vector<Entrance> generateEntrances(const Settings& caves,const EntranceSettings& e,
                                       SurfaceRectCm bounds,HeightAt heightAt,Excluded excluded) {
    validate(caves);validateEntrances(e);
    if(bounds.maxX<bounds.minX||bounds.maxZ<bounds.minZ ||
       bounds.minX< -kLimitCm||bounds.minZ< -kLimitCm||
       bounds.maxX>kLimitCm||bounds.maxZ>kLimitCm)
        throw std::invalid_argument("TerraWeave Caverns entrance bounds");
    std::vector<Entrance> result;
    if(bounds.minX==bounds.maxX||bounds.minZ==bounds.maxZ||e.chancePermille==0) return result;
    const int64_t ax=floorDiv(bounds.minX,e.spacingCm), az=floorDiv(bounds.minZ,e.spacingCm);
    const int64_t bx=floorDiv(bounds.maxX-1,e.spacingCm),bz=floorDiv(bounds.maxZ-1,e.spacingCm);
    if(bx-ax>4096||bz-az>4096||(bx-ax+1)*(bz-az+1)>4096)
        throw std::invalid_argument("TerraWeave Caverns entrance region too large");
    for(int64_t z=az;z<=bz;++z)for(int64_t x=ax;x<=bx;++x) {
        const uint64_t h=hash(e.seed,x,0,z,0xE11);
        if(h%1000>=e.chancePermille)continue;
        const int64_t px=x*e.spacingCm+e.spacingCm/2+
            (int64_t((h>>10)&65535)-32767)*e.spacingCm/8/32768;
        const int64_t pz=z*e.spacingCm+e.spacingCm/2+
            (int64_t((h>>27)&65535)-32767)*e.spacingCm/8/32768;
        if(px<bounds.minX||px>=bounds.maxX||pz<bounds.minZ||pz>=bounds.maxZ||
           excluded(px,pz))continue;
        const int64_t surface=heightAt(px,pz);
        if(surface>kLimitCm-e.radiusCm||surface< -kLimitCm+e.maxDepthCm)
            continue;
        std::optional<int64_t> target;
        for(int64_t d=e.minDepthCm;d<=e.maxDepthCm;d+=e.depthStepCm){
            const int64_t yy=surface-d;
            if(yy<caves.minYcm||yy>=caves.maxYcm)continue;
            if(caveFieldCmUnchecked(caves,{px,yy,pz})>0){target=yy;break;}
            if(e.maxDepthCm-d<e.depthStepCm)break;
        }
        if(target)result.push_back({h,px,pz,surface,*target,e.radiusCm});
    }
    return result;
}
template<class HeightAt>
std::vector<Entrance> generateEntrances(const Settings& caves,const EntranceSettings& e,
                                       SurfaceRectCm bounds,HeightAt heightAt) {
    return generateEntrances(caves,e,bounds,heightAt,
        [](int64_t,int64_t){return false;});
}
// A shaft opening is radial and extends slightly ABOVE terrain. Consumer must
// subtract the matching surface footprint from its terrain triangles.
inline int64_t entranceFieldCm(const Entrance& e,PointCm p) {
    constexpr int64_t cap=100000;
    if(p.y<e.caveYcm-e.radiusCm||p.y>e.surfaceYcm+e.radiusCm) return -cap;
    const int64_t x=p.x-e.xCm,z=p.z-e.zCm;
    const int64_t r=e.radiusCm;
    const int64_t radial=(r*r-x*x-z*z)/(2*r);
    return std::clamp<int64_t>(std::min({radial,p.y-(e.caveYcm-r),e.surfaceYcm+r-p.y}),-cap,cap);
}
inline bool surfaceHoleAt(const std::vector<Entrance>& entrances,int64_t x,int64_t z) {
    for(const auto& e:entrances) {
        const int64_t dx=x-e.xCm,dz=z-e.zCm;
        if(dx*dx+dz*dz<=int64_t(e.radiusCm)*e.radiusCm)return true;
    }
    return false;
}
inline int64_t combinedFieldCm(const Settings& caves,const std::vector<Entrance>& entrances,PointCm p) {
    int64_t v=caveFieldCmUnchecked(caves,p);
    for(const auto& e:entrances)v=std::max(v,entranceFieldCm(e,p));
    return v;
}
// Caller passes a stable superset of entrances intersecting this chunk + its
// gradient halo. Entrance list may be generated on a wider surface region.
inline CaveMesh generateCaveMeshWithEntrances(const Settings& s,const std::vector<Entrance>& entrances,
                                              ChunkKey3 key,uint32_t resolution=16,int32_t spacingCm=100) {
    return generateCaveMeshSampled(s,key,resolution,spacingCm,
        [&](PointCm p){return combinedFieldCm(s,entrances,p);});
}
} // namespace aiverse::terraweave::caverns
