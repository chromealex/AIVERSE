// Stage 7AS: validate the real Renderer3D ModelItem view/LOD selection
// before a VIVID diagnostic GPU ID pass re-renders a CPU vertex snapshot.
// A renderer-selected GPU vertex override is PROVENANCE only here: do not
// claim the CPU-expanded ID pass uses identical GPU-deformed vertex bytes.
#pragma once
#include "engine/render/Renderer3D.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::vivid {

enum class SculptGpuRendererViewReason : std::uint8_t {
    EligibleCpuSnapshot,
    WrongFrame,
    MissingModel,
    RendererUnsupportedView,
    MissingSelectedMesh,
    PrimitiveIndexMismatch,
    MultipleMeshOwners,
    InvalidMesh,
    IncompleteSelectedMesh
};

struct SculptGpuRendererViewCheck {
    SculptGpuRendererViewReason reason=
        SculptGpuRendererViewReason::MissingModel;
    // Only true if matching Renderer3D's exact pre-render selected mesh,
    // node, primitive index ranges, colour role, and source frame serial.
    bool canRenderDiagnostic=false;
    // True means Renderer3D may read a current-frame 60-byte GPU stream,
    // but the VIVID ID pass still renders CPU-reconstructed positions.
    bool gpuVertexOverridePresent=false;
    uint32_t selectedNode=UINT32_MAX;
    std::vector<std::uint32_t> otherVisibleMeshes;
};

inline const char* sculptGpuRendererViewReasonText(
        SculptGpuRendererViewReason reason) noexcept {
    switch(reason) {
        case SculptGpuRendererViewReason::EligibleCpuSnapshot:
            return "Renderer3D primitive selection matched; CPU-fed GPU diagnostic only";
        case SculptGpuRendererViewReason::WrongFrame:
            return "Renderer3D view belongs to a different frame";
        case SculptGpuRendererViewReason::MissingModel:
            return "Renderer3D did not submit the selected character model";
        case SculptGpuRendererViewReason::RendererUnsupportedView:
            return "Renderer3D chose LOD fade, GPU LOD, batch or shader vertex offset";
        case SculptGpuRendererViewReason::MissingSelectedMesh:
            return "Selected face mesh was not in the rendered colour view";
        case SculptGpuRendererViewReason::PrimitiveIndexMismatch:
            return "Renderer3D selected different primitive index ranges";
        case SculptGpuRendererViewReason::MultipleMeshOwners:
            return "Renderer3D presented ambiguous mesh instancing";
        case SculptGpuRendererViewReason::InvalidMesh:
            return "Renderer3D view referenced a missing mesh/node";
        case SculptGpuRendererViewReason::IncompleteSelectedMesh:
            return "Renderer3D omitted or duplicated a source primitive";
    }
    return "unsupported renderer view";
}

// Uses the real renderer's freshly selected ModelItem provenance. Unlike a
// parallel LOD calculator this cannot accidentally choose an unrelated LOD.
// It is still *pre*-draw candidate evidence, not GPU occlusion proof.
inline SculptGpuRendererViewCheck validateSculptGpuRendererView(
        const render::Renderer3D::ModelViewProvenance& view,
        const render::Model& source,std::size_t selectedMesh,
        std::uint64_t expectedFrame) {
    SculptGpuRendererViewCheck result;
    if(!expectedFrame || !view.frameSerial ||
       view.frameSerial!=expectedFrame) {
        result.reason=SculptGpuRendererViewReason::WrongFrame;
        return result;
    }
    if(!view.hasModel || view.primitives.empty())return result;
    if(selectedMesh>=source.meshes.size()) {
        result.reason=SculptGpuRendererViewReason::InvalidMesh;
        return result;
    }
    if(!view.complete) {
        result.reason=SculptGpuRendererViewReason::RendererUnsupportedView;
        return result;
    }
    const auto& sourcePrimitives=source.meshes[selectedMesh].primitives;
    if(sourcePrimitives.empty() || sourcePrimitives.size()>1000000u) {
        result.reason=SculptGpuRendererViewReason::IncompleteSelectedMesh;
        return result;
    }
    std::vector<std::uint8_t> selected(sourcePrimitives.size(),0);
    std::vector<std::uint32_t> owners(source.meshes.size(),UINT32_MAX);
    std::vector<std::uint64_t> instances(source.meshes.size(),0);
    std::uint32_t selectedNode=UINT32_MAX;
    for(const auto& p:view.primitives) {
        if(p.model!=&source || p.nodeIndex>=source.nodes.size() ||
           p.meshIndex>=source.meshes.size()) {
            result.reason=SculptGpuRendererViewReason::InvalidMesh;
            return result;
        }
        if(!p.colourCandidate || !p.visible || !p.indexCount ||
           !p.drawInvocation ||
           p.batched || p.gpuLodConditional || p.lodFade ||
           p.alternativeMeshLod ||
           source.nodes[p.nodeIndex].mesh!=int(p.meshIndex) ||
           p.shaderVertexOffset || p.baked || p.translucent) {
            result.reason=SculptGpuRendererViewReason::RendererUnsupportedView;
            return result;
        }
        if((owners[p.meshIndex]!=UINT32_MAX &&
            owners[p.meshIndex]!=p.nodeIndex) ||
           (instances[p.meshIndex] &&
            instances[p.meshIndex]!=p.drawInvocation)) {
            // Repeated ModelDraw submissions of one Model may share node and
            // mesh IDs but have different world transforms. One world-space
            // CPU skin surface cannot stand in for multiple instances.
            result.reason=SculptGpuRendererViewReason::MultipleMeshOwners;
            return result;
        }
        owners[p.meshIndex]=p.nodeIndex;
        instances[p.meshIndex]=p.drawInvocation;
        if(p.meshIndex!=selectedMesh)continue;
        if(p.primitiveIndex>=sourcePrimitives.size()) {
            result.reason=SculptGpuRendererViewReason::PrimitiveIndexMismatch;
            return result;
        }
        const auto& ref=sourcePrimitives[p.primitiveIndex];
        if(ref.firstIndex!=p.firstIndex ||
           ref.indexCount!=p.indexCount) {
            result.reason=SculptGpuRendererViewReason::PrimitiveIndexMismatch;
            return result;
        }
        if(selected[p.primitiveIndex]) {
            result.reason=SculptGpuRendererViewReason::IncompleteSelectedMesh;
            return result;
        }
        selected[p.primitiveIndex]=1;
        selectedNode=p.nodeIndex;
        result.gpuVertexOverridePresent |= p.customGpuStream;
        if(p.customGpuStream &&
           (!p.vertexOverride.valid()||!p.vertexOverrideCount)) {
            result.reason=SculptGpuRendererViewReason::RendererUnsupportedView;
            return result;
        }
    }
    if(selectedNode==UINT32_MAX) {
        result.reason=SculptGpuRendererViewReason::MissingSelectedMesh;
        return result;
    }
    for(auto value:selected)if(value!=1) {
        result.reason=SculptGpuRendererViewReason::IncompleteSelectedMesh;
        return result;
    }
    result.selectedNode=selectedNode;
    for(std::size_t i=0;i<owners.size();++i)
        if(i!=selectedMesh && owners[i]!=UINT32_MAX)
            result.otherVisibleMeshes.push_back(std::uint32_t(i));
    result.reason=SculptGpuRendererViewReason::EligibleCpuSnapshot;
    result.canRenderDiagnostic=true;
    return result;
}
} // namespace aiverse::vivid
