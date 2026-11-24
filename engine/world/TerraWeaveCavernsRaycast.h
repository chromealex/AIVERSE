#pragma once
// Caverns Stage20: finite, deterministic CPU ray intersection with the
// authored density zero-isosurface. Positive density is cave air.
// This is a discrete sign-change raycast, not GPU depth readback.
#include "TerraWeaveCavernsSdfSpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
struct CaveRayCm {
    PointCm origin{};
    // Unit direction in signed Q20; components must be normalized by caller.
    int32_t dxQ20=0,dyQ20=0,dzQ20=0;
};
struct CaveRayHit {
    PointCm point{};
    int64_t distanceCm=0;
    bool enteringVoid=false;
};
template<class SampleField>
inline std::optional<CaveRayHit> raycastCaveSurfaceSampled(
    SampleField&& sampleField,CaveRayCm ray,
    int64_t maxDistanceCm=10000,int64_t stepCm=50) {
    if(maxDistanceCm<1||maxDistanceCm>100000||stepCm<1||stepCm>1000)
        throw std::invalid_argument("Caverns ray distance/step");
    validatePoint(ray.origin);
    constexpr int64_t q=1LL<<20;
    const int64_t len2=int64_t(ray.dxQ20)*ray.dxQ20+
        int64_t(ray.dyQ20)*ray.dyQ20+int64_t(ray.dzQ20)*ray.dzQ20;
    if(len2<q*q-q*q/128||len2>q*q+q*q/128)
        throw std::invalid_argument("Caverns ray direction not normalized");
    const auto point=[&](int64_t t){
        return PointCm{ray.origin.x+int64_t(ray.dxQ20)*t/q,
                       ray.origin.y+int64_t(ray.dyQ20)*t/q,
                       ray.origin.z+int64_t(ray.dzQ20)*t/q};
    };
    const auto sign=[&](int64_t t){
        const auto p=point(t);
        if(p.x< -kLimitCm||p.x>kLimitCm||
           p.y< -kLimitCm||p.y>kLimitCm||
           p.z< -kLimitCm||p.z>kLimitCm)return false;
        return sampleField(p)>0;
    };
    int64_t previous=0;
    bool wasVoid=sign(0);
    for(int64_t t=stepCm;t<=maxDistanceCm;){
        const bool nowVoid=sign(t);
        if(nowVoid!=wasVoid) {
            int64_t low=previous,high=t;
            const bool initial=wasVoid;
            // Find first sign change in integer centimetres.
            while(high-low>1){
                const int64_t middle=low+(high-low)/2;
                if(sign(middle)==initial)low=middle;
                else high=middle;
            }
            return CaveRayHit{point(high),high,nowVoid};
        }
        previous=t;wasVoid=nowVoid;
        if(maxDistanceCm-t<stepCm)break;
        t+=stepCm;
    }
    return {};
}
inline std::optional<CaveRayHit> raycastCaveSurface(
    const CaveVolumeSnapshot& snapshot,CaveRayCm ray,
    int64_t maxDistanceCm=10000,int64_t stepCm=50){
    return raycastCaveSurfaceSampled(
        [&snapshot](PointCm p){return caveSnapshotFieldCm(snapshot,p);},
        ray,maxDistanceCm,stepCm);
}
// Fast path for an already indexed frozen authored SDF (e.g. TerrainWindow).
// Uses the EXACT same stepping, zero-crossing and bisection as the original.
inline std::optional<CaveRayHit> raycastCaveSurface(
    const CaveSnapshotSpatialIndex& indexed,CaveRayCm ray,
    int64_t maxDistanceCm=10000,int64_t stepCm=50){
    return raycastCaveSurfaceSampled(
        [&indexed](PointCm p){return indexed.field(p);},
        ray,maxDistanceCm,stepCm);
}
// Dabs affecting a chunk must update all neighbors touched by the volume +
// at least a gradient halo. Chunk span is resolution * spacing in cm.
struct CaveDirtyBounds {
    int64_t minX=0,minY=0,minZ=0,maxX=0,maxY=0,maxZ=0;
};
inline CaveDirtyBounds dirtyBoundsForStamp(CaveSphereStamp stamp,int32_t haloCm=100){
    if(stamp.radiusCm<1||haloCm<0||haloCm>100000)
        throw std::invalid_argument("Caverns dirty brush settings");
    const int64_t radius=int64_t(stamp.radiusCm)+haloCm;
    return {stamp.center.x-radius,stamp.center.y-radius,stamp.center.z-radius,
            stamp.center.x+radius+1,stamp.center.y+radius+1,
            stamp.center.z+radius+1};
}
inline std::vector<ChunkKey3> dirtyCaveChunks(CaveDirtyBounds b,
                                     uint32_t resolution,int32_t spacingCm,
                                     size_t maxChunks=4096){
    if(!resolution||resolution>128||spacingCm<1||maxChunks==0||
       b.maxX<=b.minX||b.maxY<=b.minY||b.maxZ<=b.minZ)
        throw std::invalid_argument("Caverns invalid dirty region");
    const int64_t span=int64_t(resolution)*spacingCm;
    const int64_t x0=floorDiv(b.minX,span),x1=floorDiv(b.maxX-1,span);
    const int64_t y0=floorDiv(b.minY,span),y1=floorDiv(b.maxY-1,span);
    const int64_t z0=floorDiv(b.minZ,span),z1=floorDiv(b.maxZ-1,span);
    const uint64_t nx=uint64_t(x1-x0+1),ny=uint64_t(y1-y0+1),nz=uint64_t(z1-z0+1);
    if(nx>maxChunks||ny>maxChunks||nz>maxChunks||
       nx*ny>maxChunks||nx*ny*nz>maxChunks)
        throw std::length_error("Caverns dirty chunk budget");
    std::vector<ChunkKey3> keys;
    keys.reserve(size_t(nx*ny*nz));
    for(int64_t z=z0;z<=z1;++z)
    for(int64_t y=y0;y<=y1;++y)
    for(int64_t x=x0;x<=x1;++x)keys.push_back({x,y,z});
    return keys;
}
} // namespace aiverse::terraweave::caverns
