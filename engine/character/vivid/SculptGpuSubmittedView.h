// Stage 7AW — match Renderer3D's *encoded* main-colour draw commands to
// its earlier staged source/LOD/instance view, never a VIVID-side guess.
// Only direct original-primitive draws are accepted by the GPU-ID oracle.
// ICB/MDI, pipeline failures, truncated or reordered model sources fail closed.
#pragma once
#include "SculptGpuRendererView.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::vivid {

enum class SculptGpuSubmittedViewReason : std::uint8_t {
    Accepted, WrongFrame, NoMainSubmission, Incomplete,
    MissingOriginalDraw, IncompatibleDraw
};

inline const char* sculptGpuSubmittedViewReasonText(
        SculptGpuSubmittedViewReason reason) noexcept {
    switch(reason) {
        case SculptGpuSubmittedViewReason::Accepted:
            return "actual Renderer3D direct main-colour draw was encoded";
        case SculptGpuSubmittedViewReason::WrongFrame:
            return "staged and encoded model draws belong to different frames";
        case SculptGpuSubmittedViewReason::NoMainSubmission:
            return "Renderer3D has not encoded the main opaque pass";
        case SculptGpuSubmittedViewReason::Incomplete:
            return "Renderer3D submission tracking truncated or unsupported";
        case SculptGpuSubmittedViewReason::MissingOriginalDraw:
            return "Renderer3D never encoded all expected model primitives";
        case SculptGpuSubmittedViewReason::IncompatibleDraw:
            return "Renderer3D encoded a different draw/LOD/pipeline";
    }
    return "unknown renderer submission";
}

struct SculptGpuSubmittedViewCheck {
    SculptGpuSubmittedViewReason reason=
        SculptGpuSubmittedViewReason::NoMainSubmission;
    bool accepted=false;
    std::size_t checkedPrimitives=0;
};

// This is strictly a *command-encoding* fact. It is NOT a framebuffer/depth
// visibility result; async GPU ID pixels and the CPU BVH continue to have
// their own independently validated frame/revision/click tokens.
inline SculptGpuSubmittedViewCheck validateSculptGpuSubmittedView(
        const render::Renderer3D::ModelViewProvenance& staged,
        const render::Renderer3D::ModelSubmissionProvenance& submitted,
        const render::Model& source,std::size_t selectedMesh,
        std::uint64_t expectedFrame) {
    using Renderer=render::Renderer3D;
    SculptGpuSubmittedViewCheck out;
    if(!expectedFrame || staged.frameSerial!=expectedFrame ||
       submitted.frameSerial!=expectedFrame) {
        out.reason=SculptGpuSubmittedViewReason::WrongFrame;
        return out;
    }
    if(!submitted.mainPassEncoded) {
        out.reason=SculptGpuSubmittedViewReason::NoMainSubmission;
        return out;
    }
    if(!staged.complete || !staged.hasModel || submitted.truncated ||
       staged.primitives.empty() || staged.primitives.size()>4096u ||
       submitted.commands.size()!=staged.primitives.size() ||
       selectedMesh>=source.meshes.size()) {
        out.reason=SculptGpuSubmittedViewReason::Incomplete;
        return out;
    }
    std::vector<std::uint8_t> matched(submitted.commands.size(),0);
    bool selectedSeen=false;
    for(const auto& p:staged.primitives) {
        if(p.model!=&source || !p.colourCandidate || !p.visible ||
           !p.indexCount || !p.drawInvocation) {
            out.reason=SculptGpuSubmittedViewReason::IncompatibleDraw;
            return out;
        }
        bool found=false;
        for(std::size_t i=0;i<submitted.commands.size();++i) {
            if(matched[i])continue;
            const auto& cmd=submitted.commands[i];
            const auto& q=cmd.source;
            if(q.drawInvocation!=p.drawInvocation ||
               q.nodeIndex!=p.nodeIndex || q.meshIndex!=p.meshIndex ||
               q.primitiveIndex!=p.primitiveIndex)
                continue;
            // Matching source identity but a different command means no
            // exact GPU-source correspondence. Do not try another command.
            if(cmd.phase!=1u ||
               cmd.kind!=Renderer::ModelSubmissionKind::Direct ||
               !cmd.pipeline.valid() ||
               q.model!=p.model ||
               q.firstIndex!=p.firstIndex ||
               q.indexCount!=p.indexCount ||
               q.vertexOverride!=p.vertexOverride ||
               q.vertexOverrideCount!=p.vertexOverrideCount ||
               q.skinning!=p.skinning || q.baked!=p.baked ||
               q.customGpuStream!=p.customGpuStream ||
               q.maskedMaterial!=p.maskedMaterial ||
               (p.maskedMaterial &&
                (q.maskBaseTexture!=p.maskBaseTexture ||
                 q.maskAlpha!=p.maskAlpha ||
                 q.maskCutoff!=p.maskCutoff ||
                 q.maskUvOffset.x!=p.maskUvOffset.x ||
                 q.maskUvOffset.y!=p.maskUvOffset.y ||
                 q.maskUvScale.x!=p.maskUvScale.x ||
                 q.maskUvScale.y!=p.maskUvScale.y ||
                 q.maskUvRotation!=p.maskUvRotation ||
                 q.maskVertexColors!=p.maskVertexColors)) ||
               q.batched || q.gpuLodConditional || q.lodFade ||
               q.alternativeMeshLod || q.shaderVertexOffset ||
               q.translucent || !q.colourCandidate) {
                out.reason=SculptGpuSubmittedViewReason::IncompatibleDraw;
                return out;
            }
            for(std::size_t c=0;c<16;++c)
                if(q.world.m[c]!=p.world.m[c]) {
                    out.reason=SculptGpuSubmittedViewReason::IncompatibleDraw;
                    return out;
                }
            matched[i]=1;
            found=true;
            ++out.checkedPrimitives;
            if(p.meshIndex==selectedMesh)selectedSeen=true;
            break;
        }
        if(!found) {
            out.reason=SculptGpuSubmittedViewReason::MissingOriginalDraw;
            return out;
        }
    }
    if(!selectedSeen) {
        out.reason=SculptGpuSubmittedViewReason::MissingOriginalDraw;
        return out;
    }
    out.reason=SculptGpuSubmittedViewReason::Accepted;
    out.accepted=true;
    return out;
}
// Stage 7AZ: verify both actually encoded independent actor draws
// before an ID pass borrows two separate renderer-owned 60B streams.
// This checks frame/primitive/LOD/instance provenance, not hardware
// depth or pixel visibility (which the native Metal test measures).
inline bool sculptGpuPairedRendererViewsAccepted(
        const render::Renderer3D::ModelViewProvenance& selectedView,
        const render::Renderer3D::ModelSubmissionProvenance& selectedEncoded,
        const render::Model& selectedModel,std::size_t selectedMesh,
        const render::Renderer3D::ModelViewProvenance& blockerView,
        const render::Renderer3D::ModelSubmissionProvenance& blockerEncoded,
        const render::Model& blockerModel,std::size_t blockerMesh,
        std::uint64_t frame) {
    if(&selectedModel==&blockerModel || !frame ||
       !validateSculptGpuSubmittedView(
           selectedView,selectedEncoded,selectedModel,selectedMesh,frame).accepted ||
       !validateSculptGpuSubmittedView(
           blockerView,blockerEncoded,blockerModel,blockerMesh,frame).accepted)
        return false;
    const auto originalStream=[](
            const render::Renderer3D::ModelViewProvenance& view,
            const render::Model& model,
            std::size_t mesh) noexcept -> gfx::BufferHandle {
        gfx::BufferHandle stream{};
        bool found=false;
        for(const auto& p:view.primitives) {
            if(p.model!=&model || p.meshIndex!=mesh)continue;
            if(!p.customGpuStream || !p.vertexOverride.valid() ||
               !p.vertexOverrideCount ||
               (stream.valid() && stream!=p.vertexOverride))
                return {};
            stream=p.vertexOverride;
            found=true;
        }
        return found?stream:gfx::BufferHandle{};
    };
    const auto a=originalStream(selectedView,selectedModel,selectedMesh);
    const auto b=originalStream(blockerView,blockerModel,blockerMesh);
    return a.valid() && b.valid() && a!=b;
}

} // namespace aiverse::vivid
