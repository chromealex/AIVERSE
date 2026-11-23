#pragma once
// Stage 15: safe per-face LOD publication policy.
// No scene is promoted to visible until its actual shared borders have been
// certified. When the collinear transition helper declines a face, force both
// resident chunks to the finest common lattice and regenerate their meshes.
// Visual-only; STRATA does not own authoritative collision.
#include "engine/world/TerraWeaveCavernsLodTransition.h"
#include "engine/world/TerraWeaveCavernsLodWeld.h"
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {
enum class CaveFaceDecision : uint8_t {
    ExactMatch = 0,
    Subdivided = 1,
    RegenerateAtCommonLod = 2
};
struct CaveFaceSpec {
    const CaveMesh* first=nullptr;
    uint32_t firstResolution=0;
    int32_t firstSpacingCm=0;
    const CaveMesh* second=nullptr;
    uint32_t secondResolution=0;
    int32_t secondSpacingCm=0;
};
struct CaveFaceResolution {
    CaveFaceDecision decision=CaveFaceDecision::RegenerateAtCommonLod;
    CaveMesh firstMesh{},secondMesh{};
    uint32_t commonResolution=0;
    int32_t commonSpacingCm=0;
    unsigned axis=0;
};
// first and second can be supplied in either order, on any of six adjacent
// face directions. Only the coarse-on-negative-side case can currently use
// the stage-14 conservative collinear transition; all other non-exact mixed
// surfaces are regenerated on one lattice (no unsupported invented triangles).
inline CaveFaceResolution resolveCaveFaceLod(const CaveFaceSpec& input) {
    if(!input.first||!input.second||!input.firstResolution||
       !input.secondResolution||input.firstResolution>64||
       input.secondResolution>64||input.firstSpacingCm<=0||
       input.secondSpacingCm<=0)
        throw std::invalid_argument("Caverns face spec");
    const int64_t span=int64_t(input.firstResolution)*input.firstSpacingCm;
    if(span!=int64_t(input.secondResolution)*input.secondSpacingCm)
        throw std::invalid_argument("Caverns face spans differ");
    unsigned axis=3;
    int direction=0;
    const auto a=std::array<int64_t,3>{input.first->key.x,input.first->key.y,input.first->key.z};
    const auto b=std::array<int64_t,3>{input.second->key.x,input.second->key.y,input.second->key.z};
    for(unsigned d=0;d<3;++d) {
        const int64_t delta=b[d]-a[d];
        if(delta!=0) {
            if(axis!=3|| (delta!=1&&delta!=-1))
                throw std::invalid_argument("Caverns face nonadjacent chunks");
            axis=d;direction=delta>0?1:-1;
        }
    }
    if(axis==3)throw std::invalid_argument("Caverns face identical chunks");
    const CaveMesh* low=direction>0?input.first:input.second;
    const CaveMesh* high=direction>0?input.second:input.first;
    const uint32_t lr=direction>0?input.firstResolution:input.secondResolution;
    const uint32_t hr=direction>0?input.secondResolution:input.firstResolution;
    const int32_t ls=direction>0?input.firstSpacingCm:input.secondSpacingCm;
    const int32_t hs=direction>0?input.secondSpacingCm:input.firstSpacingCm;
    const int64_t plane=(a[axis]+(direction>0?1:0))*span;
    CaveFaceResolution result;
    result.axis=axis;
    result.firstMesh=*input.first;
    result.secondMesh=*input.second;
    const int32_t spacing=std::min(ls,hs);
    if(span%spacing||span/spacing>64)
        throw std::invalid_argument("Caverns common LOD out of range");
    result.commonSpacingCm=spacing;
    result.commonResolution=uint32_t(span/spacing);
    const auto le=caveBoundaryEdges(*low,lr,ls,axis,plane);
    const auto he=caveBoundaryEdges(*high,hr,hs,axis,plane);
    if(le==he) {result.decision=CaveFaceDecision::ExactMatch;return result;}
    if(ls>hs) {
        const auto transition=stitchCaveCollinearBoundary(*low,lr,ls,*high,hr,hs,axis);
        if(transition.supported) {
            if(direction>0)result.firstMesh=transition.coarseStitched;
            else result.secondMesh=transition.coarseStitched;
            result.decision=CaveFaceDecision::Subdivided;
            return result;
        }
    }
    result.decision=CaveFaceDecision::RegenerateAtCommonLod;
    return result;
}
// Publication must reject stale mesh pairs even when a previous pair was
// verified. Revisions are a HOST contract, not inferred from mesh vertices.
struct CaveFaceLease {
    ChunkKey3 first{},second{};
    uint64_t firstRevision=0,secondRevision=0;
    CaveFaceDecision decision=CaveFaceDecision::RegenerateAtCommonLod;
    uint32_t commonResolution=0;
    int32_t commonSpacingCm=0;
};
inline bool caveFaceLeaseCurrent(const CaveFaceLease& lease,
                                 uint64_t firstRevision,uint64_t secondRevision){
    return firstRevision!=0&&secondRevision!=0&&
           lease.firstRevision==firstRevision&&lease.secondRevision==secondRevision;
}
} // namespace aiverse::terraweave::caverns
