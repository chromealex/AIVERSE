#pragma once
// TerraWeave Caverns Stage37: crack-safe, six-direction mixed-LOD visual plan.
// All vertices are generated from immutable CPU cave meshes. No GPU or
// authoritative Box3D/SpatialWorld geometry is modified in this planner.
// Compatible collinear borders are subdivided on the coarse side; divergent
// contours must be regenerated at the finest shared lattice BEFORE publish.
#include "engine/world/TerraWeaveCavernsSeamGate.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {

struct CaveMixedLodFace {
    CaveFaceKey keys{};
    unsigned axis=0;
    CaveFaceDecision decision=CaveFaceDecision::ExactMatch;
    uint32_t targetResolution=0;
    int32_t targetSpacingCm=0;
};
struct CaveMixedLodPlan {
    std::map<ChunkKey3,CaveCertifiedChunk,KeyLess> visual;
    std::vector<CaveMixedLodFace> faces;
    std::map<ChunkKey3,CaveRebuildRequest,KeyLess> rebuild;
    bool sourceConflict=false;
    bool ready()const noexcept{return !sourceConflict&&rebuild.empty();}
};

inline CaveMixedLodPlan planCaveMixedLodVisual(
    const std::vector<CaveCertifiedChunk>& residents,
    size_t maxChunks=1024) {
    if(!maxChunks||maxChunks>8192||residents.size()>maxChunks)
        throw std::invalid_argument("Caverns visual LOD plan budget");
    CaveMixedLodPlan plan;
    for(const auto& c:residents) {
        if(!c.sourceRevision||!c.renderRevision||!c.resolution||
           c.resolution>64||c.spacingCm<=0||c.spacingCm>10000)
            throw std::invalid_argument("Caverns visual LOD source");
        if(!plan.visual.emplace(c.mesh.key,c).second)
            throw std::invalid_argument("Caverns duplicate visual LOD source");
    }
    constexpr std::array<ChunkKey3,3> axes={ChunkKey3{1,0,0},
        ChunkKey3{0,1,0},ChunkKey3{0,0,1}};
    // A stitched mesh may have shared corner vertices with another face.
    // Process in deterministic X/Y/Z order, retaining the other-face edits.
    for(const auto& [key,original]:plan.visual){
        (void)original;
        for(unsigned axis=0;axis<3;++axis){
            const auto d=axes[axis];
            const ChunkKey3 other{key.x+d.x,key.y+d.y,key.z+d.z};
            auto jt=plan.visual.find(other);
            if(jt==plan.visual.end())continue;
            auto it=plan.visual.find(key);
            auto& first=it->second;
            auto& second=jt->second;
            const int64_t span=int64_t(first.resolution)*first.spacingCm;
            if(span!=int64_t(second.resolution)*second.spacingCm)
                throw std::invalid_argument("Caverns mixed LOD unequal chunk span");
            CaveMixedLodFace face;
            face.keys={key,other};
            face.axis=axis;
            face.targetSpacingCm=std::min(first.spacingCm,second.spacingCm);
            face.targetResolution=uint32_t(span/face.targetSpacingCm);
            if(first.sourceRevision!=second.sourceRevision){
                // Mixed authored densities could have divergent contours even
                // with identical LOD. Do not bridge unrelated SDF snapshots.
                plan.sourceConflict=true;
                face.decision=CaveFaceDecision::RegenerateAtCommonLod;
                plan.faces.push_back(face);
                continue;
            }
            auto outcome=resolveCaveFaceLod({
                &first.mesh,first.resolution,first.spacingCm,
                &second.mesh,second.resolution,second.spacingCm});
            face.decision=outcome.decision;
            if(outcome.decision==CaveFaceDecision::Subdivided){
                if(first.spacingCm>second.spacingCm)
                    first.mesh=std::move(outcome.firstMesh);
                else second.mesh=std::move(outcome.secondMesh);
            }else if(outcome.decision==CaveFaceDecision::RegenerateAtCommonLod){
                // Invalid mixed contours require the same density snapshot
                // sampled at a shared lattice, not a fabricated bridge.
                const auto request=[&](const CaveCertifiedChunk& c){
                    const auto old=plan.rebuild.find(c.mesh.key);
                    if(old==plan.rebuild.end() ||
                       old->second.spacingCm>face.targetSpacingCm)
                        plan.rebuild[c.mesh.key]=CaveRebuildRequest{
                            c.mesh.key,face.targetResolution,
                            face.targetSpacingCm,c.sourceRevision,0};
                };
                request(first);request(second);
            }
            plan.faces.push_back(face);
        }
    }
    // Final cross-face verification is essential: patching a coarse triangle
    // on face +X may also affect its +Y border. No partially certified batch
    // is allowed to go to GPU staging after the full set of patches.
    for(const auto& face:plan.faces){
        if(face.decision==CaveFaceDecision::RegenerateAtCommonLod)continue;
        const auto& a=plan.visual.at(face.keys.negative);
        const auto& b=plan.visual.at(face.keys.positive);
        const int64_t span=int64_t(a.resolution)*a.spacingCm;
        const int64_t plane=
            (face.axis==0?face.keys.positive.x:
             face.axis==1?face.keys.positive.y:
                          face.keys.positive.z)*span;
        const auto firstEdges=caveBoundaryEdges(
            a.mesh,a.resolution,a.spacingCm,face.axis,plane);
        const auto secondEdges=caveBoundaryEdges(
            b.mesh,b.resolution,b.spacingCm,face.axis,plane);
        if(firstEdges!=secondEdges){
            const auto request=[&](const CaveCertifiedChunk& c){
                const auto old=plan.rebuild.find(c.mesh.key);
                if(old==plan.rebuild.end() ||
                   old->second.spacingCm>face.targetSpacingCm)
                    plan.rebuild[c.mesh.key]=CaveRebuildRequest{
                        c.mesh.key,face.targetResolution,
                        face.targetSpacingCm,c.sourceRevision,0};
            };
            request(a);request(b);
        }
    }
    return plan;
}
} // namespace aiverse::terraweave::caverns
