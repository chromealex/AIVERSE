#pragma once
// TerraWeave Caverns Stage18: immutable, versioned density snapshot.
// Authoritative gameplay should persist the inputs, never GPU mesh bytes.
// All coordinates are signed centimetres, positive field = empty cave.
#include "TerraWeaveCavernsEntrances.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {

enum class CaveStampMode : uint8_t { Carve=0, Fill=1 };
struct CaveSphereStamp {
    uint64_t id=0; // Stable authored identity, unique per snapshot.
    PointCm center{};
    int32_t radiusCm=100;
    CaveStampMode mode=CaveStampMode::Carve;
    constexpr bool operator==(const CaveSphereStamp&)const=default;
};
struct CaveVolumeSnapshot {
    uint64_t generatorVersion=1;
    uint64_t sourceRevision=1;
    Settings settings{};
    std::vector<Entrance> entrances; // Entire relevant influence region, not only current tile
    std::vector<CaveSphereStamp> stamps;
};
// Validation and canonical ordering happen once on the scene owner thread.
// The same snapshot must be used for EVERY adjacent chunk and worker.
inline void canonicalizeCaveSnapshot(CaveVolumeSnapshot& snapshot) {
    if(!snapshot.generatorVersion||!snapshot.sourceRevision)
        throw std::invalid_argument("Caverns snapshot revision");
    validate(snapshot.settings);
    if(snapshot.stamps.size()>16384||snapshot.entrances.size()>4096)
        throw std::length_error("Caverns snapshot element budget");
    std::sort(snapshot.stamps.begin(),snapshot.stamps.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    for(size_t i=0;i<snapshot.stamps.size();++i){
        const auto& s=snapshot.stamps[i];
        validatePoint(s.center);
        if(s.radiusCm<1||s.radiusCm>1000000||
           (s.mode!=CaveStampMode::Carve&&s.mode!=CaveStampMode::Fill)||
           (i&&snapshot.stamps[i-1].id==s.id))
            throw std::invalid_argument("Caverns stamp invalid/duplicate");
    }
    std::sort(snapshot.entrances.begin(),snapshot.entrances.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    for(size_t i=0;i<snapshot.entrances.size();++i){
        const auto& e=snapshot.entrances[i];
        if(e.radiusCm<=0||e.radiusCm>1000000||
           e.surfaceYcm<e.caveYcm||
           (i&&snapshot.entrances[i-1].id==e.id))
            throw std::invalid_argument("Caverns entrance invalid/duplicate");
    }
}
// Squared-radius approximation with bounded differences. Prevents overflow
// even when sampling very distant authored stamps; no floating-point RNG.
inline int64_t caveSphereField(const CaveSphereStamp& s,PointCm p) {
    const int64_t dx=p.x-s.center.x,dy=p.y-s.center.y,dz=p.z-s.center.z;
    const int64_t r=s.radiusCm;
    if(dx>r||dx< -r||dy>r||dy< -r||dz>r||dz< -r)return -100000;
    return std::clamp<int64_t>(
        (r*r-dx*dx-dy*dy-dz*dz)/(2*r),-100000,100000);
}
// One immutable field source, shared by all chunk generations.
// Fill always wins over carve where fields overlap (independent of order).
inline int64_t caveSnapshotFieldCm(const CaveVolumeSnapshot& snapshot,PointCm p){
    int64_t cave=combinedFieldCm(snapshot.settings,snapshot.entrances,p);
    for(const auto& stamp:snapshot.stamps)
        if(stamp.mode==CaveStampMode::Carve)
            cave=std::max(cave,caveSphereField(stamp,p));
    for(const auto& stamp:snapshot.stamps)
        if(stamp.mode==CaveStampMode::Fill)
            cave=std::min(cave,-caveSphereField(stamp,p));
    return cave;
}
inline CaveMesh generateCaveSnapshotMesh(const CaveVolumeSnapshot& snapshot,
                                          ChunkKey3 key,uint32_t resolution,
                                          int32_t spacingCm) {
    return generateCaveMeshSampled(snapshot.settings,key,resolution,spacingCm,
        [&snapshot](PointCm p){return caveSnapshotFieldCm(snapshot,p);});
}
} // namespace aiverse::terraweave::caverns
