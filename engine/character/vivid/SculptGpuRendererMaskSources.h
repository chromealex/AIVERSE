// Stage 7BA: renderer-derived bounded set of independent GPU Mask blockers.
#pragma once
#include "SculptGpuSubmittedView.h"
#include "SculptGpuVertexSource.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

namespace aiverse::vivid {

struct SculptGpuMaskSceneCandidate {
    const render::Model* model=nullptr;
    std::size_t mesh=0;
    std::span<const render::mat4> nodeWorld{};
};

// The host supplies a model and MotionWeave pose. The actual visible
// geometry/LOD, 60B GPU stream and accepted main-colour draw identity MUST
// come from Renderer3D's current-frame staged/submitted snapshots.
inline bool prepareSculptGpuRendererMaskOccluder(
        const render::Renderer3D::ModelViewProvenance& staged,
        const render::Renderer3D::ModelSubmissionProvenance& submitted,
        const SculptGpuMaskSceneCandidate& candidate,
        std::uint64_t frame,gfx::BufferHandle selectedGpuBuffer,
        SculptGpuVertexSource& output) {
    if(!candidate.model || !selectedGpuBuffer.valid() || !frame ||
       candidate.mesh>=candidate.model->meshes.size() ||
       candidate.nodeWorld.size()!=candidate.model->nodes.size() ||
       !validateSculptGpuSubmittedView(
           staged,submitted,*candidate.model,candidate.mesh,frame).accepted)
        return false;
    const render::Renderer3D::ModelViewPrimitive* first=nullptr;
    for(const auto& p:staged.primitives) {
        if(p.model!=candidate.model || p.meshIndex!=candidate.mesh)continue;
        if(!p.colourCandidate || !p.customGpuStream ||
           !p.vertexOverride.valid() ||
           p.vertexOverride==selectedGpuBuffer ||
           p.vertexOverrideCount!=candidate.model->vertices.size())
            return false;
        if(first) {
            if(p.vertexOverride!=first->vertexOverride ||
               p.vertexOverrideCount!=first->vertexOverrideCount ||
               p.drawInvocation!=first->drawInvocation ||
               p.skinning!=first->skinning)return false;
            for(std::size_t c=0;c<16;++c)
                if(p.world.m[c]!=first->world.m[c])return false;
        } else first=&p;
    }
    if(!first)return false;
    SculptGpuVertexSource proposed;
    if(!first->maskedMaterial || !first->maskBaseTexture.valid() ||
       !prepareSculptGpuVertexSource(
            *candidate.model,candidate.mesh,candidate.nodeWorld,
            first->world,first->vertexOverride,
            first->vertexOverrideCount,{},candidate.model->vertices.size(),
            proposed) ||
       !sculptGpuMaskedOccluderInputsValid(
           proposed,selectedGpuBuffer) ||
       first->skinning!=(proposed.params.skinned!=0u) ||
       proposed.maskBaseTexture!=first->maskBaseTexture ||
       proposed.maskUniform.alpha[0]!=first->maskAlpha ||
       proposed.maskUniform.alpha[1]!=first->maskCutoff ||
       proposed.maskUniform.uv0[0]!=first->maskUvOffset.x ||
       proposed.maskUniform.uv0[1]!=first->maskUvOffset.y ||
       proposed.maskUniform.uv0[2]!=first->maskUvScale.x ||
       proposed.maskUniform.uv0[3]!=first->maskUvScale.y ||
       proposed.maskUniform.uv1[0]!=first->maskUvRotation)
        return false;
    output=std::move(proposed);
    return true;
}

// Called after normal Renderer3D::render(). Candidates must be opted into
// trackModelSubmissionsMany BEFORE staging drawModel. A malformed, stale,
// duplicated or untracked candidate invalidates the whole transaction;
// skipping it would risk claiming a GPU ID for an actually hidden vertex.
inline bool collectSculptGpuRendererMaskOccluders(
        const render::Renderer3D& renderer,
        std::span<const SculptGpuMaskSceneCandidate> visibleCandidates,
        std::uint64_t frame,gfx::BufferHandle selectedGpuBuffer,
        std::vector<SculptGpuVertexSource>& output) {
    if(!frame || !selectedGpuBuffer.valid() ||
       visibleCandidates.size()>kSculptGpuMaxMaskOccluders)
        return false;
    std::vector<SculptGpuVertexSource> proposed;
    proposed.reserve(visibleCandidates.size());
    std::vector<const render::Model*> owners;
    owners.reserve(visibleCandidates.size());
    for(const auto& candidate:visibleCandidates) {
        if(!candidate.model)return false;
        for(const auto* owner:owners)
            if(owner==candidate.model)return false;
        owners.push_back(candidate.model);
        SculptGpuVertexSource source;
        if(!prepareSculptGpuRendererMaskOccluder(
                renderer.modelViewProvenance(*candidate.model),
                renderer.modelSubmissionProvenance(*candidate.model),
                candidate,frame,selectedGpuBuffer,source))
            return false;
        for(const auto& prior:proposed)
            if(prior.actualVertexOverride==source.actualVertexOverride)
                return false;
        proposed.push_back(std::move(source));
    }
    output=std::move(proposed);
    return true;
}

} // namespace aiverse::vivid
