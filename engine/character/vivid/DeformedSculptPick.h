// VIVID CPU reference for picking the *visible* imported source surface.
// Sparse morph deformation is applied in bind space, before MotionWeave
// skinning, matching VIVID's GPU deformed ModelVertex path. This is an
// authoring tool; never mutates the game ECS or source Model.
#pragma once
#include "SkinnedWeightPick.h"
#include "SkinSeamSplit.h"
#include "FaceShapeBinding.h"
#include <cmath>
#include <limits>
#include <span>
#include <vector>
#include <utility>

namespace aiverse::vivid {
// Build once per successful imported-model candidate publication. Generated
// LOD vertices remain unselectable; only seam-split clones map back to an
// original authorable vertex. Reject invalid/duplicated clone destinations
// without modifying the currently published mapping.
inline bool buildSculptSourceVertexMap(
        std::size_t modelVertices,std::size_t sourceVertices,
        std::span<const SkinSeamSplit::Clone> clones,
        std::vector<std::uint32_t>& output) {
    if(!sourceVertices || sourceVertices>modelVertices ||
       modelVertices>1000000u ||
       sourceVertices>std::numeric_limits<std::uint32_t>::max())
        return false;
    std::vector<std::uint32_t> mapped(
        modelVertices,std::numeric_limits<std::uint32_t>::max());
    for(std::size_t i=0;i<sourceVertices;++i)
        mapped[i]=static_cast<std::uint32_t>(i);
    std::vector<std::uint8_t> assigned(modelVertices,0);
    for(const auto& clone:clones) {
        // Seams on authored LOD vertices can also be split. Keep those
        // clones non-authorable, but reject duplicate or invalid targets.
        if(clone.source>=modelVertices || clone.target<sourceVertices ||
           clone.target>=modelVertices || assigned[clone.target])
            return false;
        assigned[clone.target]=1;
        mapped[clone.target]=clone.source<sourceVertices
            ?clone.source:std::numeric_limits<std::uint32_t>::max();
    }
    output=std::move(mapped);
    return true;
}


// Returns false without changing output on any invalid library/link, weight
// or nonfinite bind-space result. The GPU's active published morph library
// must be passed, not an uncommitted editor draft.
inline bool prepareSculptMorphPositions(
        const render::Model& model,
        const SparseMorphLibrary& library,
        std::span<const float> weights,
        std::vector<render::float3>& output,
        std::vector<render::float3>* scratch=nullptr) {
    if(library.vertices()!=model.vertices.size() ||
       weights.size()!=library.targets() ||
       model.vertices.size()>1000000u)return false;
    for(float w:weights)if(!std::isfinite(w)||w<0.f||w>1.f)return false;
    const auto offsets=library.gpuVertexOffsets();
    const auto links=library.gpuVertexLinks();
    const auto deltas=library.gpuDeltas();
    if(offsets.size()!=model.vertices.size()+1 ||
       offsets.front()!=0 || offsets.back()!=links.size())return false;
    if(scratch==&output)return false;
    std::vector<render::float3> local;
    auto& candidate=scratch?*scratch:local;
    candidate.clear();
    candidate.reserve(model.vertices.size());
    for(std::size_t i=0;i<model.vertices.size();++i) {
        auto p=model.vertices[i].pos;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            return false;
        if(offsets[i]>offsets[i+1] || offsets[i+1]>links.size())return false;
        for(auto j=offsets[i];j<offsets[i+1];++j) {
            const auto& link=links[j];
            if(link.target>=weights.size()||link.delta>=deltas.size()||
               deltas[link.delta].vertex!=i)return false;
            const auto& d=deltas[link.delta];
            const auto w=weights[link.target];
            p+=render::float3{d.position[0],d.position[1],d.position[2]}*w;
        }
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            return false;
        candidate.push_back(p);
    }
    if(scratch)std::swap(output,candidate);
    else output=std::move(candidate);
    return true;
}

// One-on-click CPU reconstruction; do not run on every render frame.
// The picked index is always in the original author's vertex ID space.
// Fail closed instead of silently ray-casting the undeformed mesh when a
// pose, palette, morph route, source ID or deformed position is invalid.
inline SculptPick pickDeformedSculptVertex(
        const render::Model& model,
        std::size_t meshId,
        std::span<const render::mat4> nodeWorld,
        const SparseMorphLibrary& morphs,
        const FaceShapeBinding& binding,
        const FaceFrame& face,
        render::float3 origin,
        render::float3 direction,
        std::size_t sourceVertexCount,
        std::span<const std::uint32_t> sourceVertexIds={}) {
    SculptPick none;
    if(sourceVertexCount==0 || sourceVertexCount>model.vertices.size() ||
       binding.targets()!=morphs.targets())return none;
    std::vector<float> weights(morphs.targets());
    if(!binding.evaluate(face,weights))return none;
    std::vector<render::float3> deformedBind;
    if(!prepareSculptMorphPositions(model,morphs,weights,deformedBind))
        return none;
    SkinnedWeightSurface surface;
    if(!prepareSkinnedWeightSurface(model,meshId,nodeWorld,
                                    surface,deformedBind))return none;
    return pickSkinnedWeightVertex(model,surface,origin,direction,
                                   sourceVertexCount,sourceVertexIds);
}

} // namespace aiverse::vivid
