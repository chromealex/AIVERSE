#pragma once
// Cave mesh presentation contract for STRATA. Pure revision routing metadata.
// No STRATA GPU APIs here: GeometryScene remains the sole STRATA owner.
#include "engine/render/TerraWeaveCavernsResident.h"
namespace aiverse::terraweave::caverns {
enum class CaveGeometryRoute : uint8_t { Renderer3DFallback=0, StrataEligible=1 };
struct CaveGeometryLease {
    ChunkKey3 key{};
    uint64_t generatorVersion=1;
    uint64_t sourceRevision=0;
    uint64_t renderRevision=0;
    CaveGeometryRoute route=CaveGeometryRoute::Renderer3DFallback;
};
// Eligibility DOES NOT mean STRATA has acquired geometry. The GeometryScene
// owner must independently verify mesh topology/material safety, cluster LOD,
// GPU residency and exactly-once draw ownership before suppressing fallback.
inline CaveGeometryLease planCaveStrataLease(
        ChunkKey3 key,uint64_t generatorVersion,uint64_t sourceRevision,
        uint64_t renderRevision,bool strataEnabled,bool isStatic,
        bool topologyValidated,bool hasCommittedGeometry) {
    CaveGeometryLease out{key,generatorVersion,sourceRevision,renderRevision,
                          CaveGeometryRoute::Renderer3DFallback};
    if(generatorVersion==0||renderRevision==0||!hasCommittedGeometry)
        return out;
    if(strataEnabled&&isStatic&&topologyValidated)
        out.route=CaveGeometryRoute::StrataEligible;
    return out;
}
// Any source mutation invalidates an old cluster/LOD candidate, even if its
// source chunk key remains identical (edited cave, changed entrance etc.).
inline bool caveStrataLeaseCurrent(const CaveGeometryLease& lease,
                                   uint64_t generatorVersion,
                                   uint64_t sourceRevision,uint64_t renderRevision) {
    return lease.generatorVersion==generatorVersion &&
           lease.sourceRevision==sourceRevision &&
           lease.renderRevision==renderRevision;
}
} // namespace aiverse::terraweave::caverns
