#include "StrataVulkanIndirectContract.h"

#include <cmath>
#include <limits>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error,const char* message) {
    if(error)*error=message;
    return false;
}
}
bool prepareVulkanIndirectPrimitiveContract(
        gfx::Device& device,const Model& source,const ModelAsset& staged,
        const GpuMetadataScene::IndexBinding& binding,
        uint32_t primitiveOrdinal,const GpuPassConfig& view,
        bool hasShadowOrReflection,
        VulkanIndirectContract& out,GpuPassConfig& guardedView,
        std::string* error) {
    if(error)error->clear();
    if(device.info().backend!=gfx::Backend::Vulkan ||
       !device.supportsCompute() || !device.supportsMultiDrawIndirect() ||
       !device.gpuHealthy() || !device.frameSerial() ||
       hasShadowOrReflection || !binding.valid() ||
       staged.primitives.empty() || staged.primitives.size()>16 ||
       !staged.skipped.empty() ||
       !source.geometryPoolPage || !source.vb.valid() ||
       source.vb!=source.ib || source.vb!=binding.buffer ||
       !source.skins.empty() || !source.clips.empty() ||
       !source.variants.empty() || source.morphTex.valid() ||
       source.geometry || source.baked.tex.valid() ||
       source.nodes.size()!=1 || source.order.size()!=1 ||
       source.order.front()!=0 || source.bindWorld.size()!=1 ||
       source.meshes.size()!=1 || source.materials.empty() ||
       source.nodes.front().parent!=-1 || source.nodes.front().skin>=0 ||
       source.nodes.front().runtimeMutable ||
       !source.nodes.front().children.empty() ||
       source.nodes.front().mesh!=0)
        return fail(error,"Vulkan indirect requires one rigid pooled mesh and main view");

    const auto& mesh=source.meshes.front();
    if(mesh.deformable || !mesh.lods.empty() ||
       mesh.primitives.empty() || mesh.primitives.size()>16 ||
       staged.primitives.size()!=mesh.primitives.size() ||
       primitiveOrdinal>=staged.primitives.size())
        return fail(error,"Vulkan indirect source has incompatible primitive hierarchy");

    // Check EVERY source primitive against its stage record, even when this
    // call selects only one. This prevents an unrelated material/batch from
    // changing the final ModelItem layout and silently double-drawing.
    uint64_t totalStagedIndices=0;
    for(size_t index=0;index<staged.primitives.size();++index){
        const auto& entry=staged.primitives[index];
        const auto& p=mesh.primitives[index];
        if(entry.mesh!=0 || entry.primitive!=index ||
           entry.material>=source.materials.size() ||
           p.material!=int(entry.material) ||
           !p.indexCount || p.indexCount%3!=0 ||
           p.morphTargets || !p.jointPalette.empty() ||
           !p.variantMaterial.empty())
            return fail(error,"Vulkan indirect source/staged primitive identity mismatch");
        const auto& material=source.materials[entry.material];
        if(material.alpha!=AlphaMode::Opaque || material.transparent ||
           material.shaderGraph)
            return fail(error,"Vulkan indirect rejects alpha or authored material shaders");
        const uint64_t sourceEnd=uint64_t(p.firstIndex)+p.indexCount;
        if(sourceEnd*sizeof(uint32_t)>source.gpuPoolIndexBytes)
            return fail(error,"Vulkan indirect original pooled index span invalid");
        if(!entry.geometry.logicalIndexCount() ||
           totalStagedIndices>UINT32_MAX-entry.geometry.logicalIndexCount())
            return fail(error,"Vulkan indirect staged index byte offsets overflow");
        totalStagedIndices+=entry.geometry.logicalIndexCount();
    }
    if(totalStagedIndices!=binding.indexCount ||
       source.gpuBaseVertex>uint32_t(std::numeric_limits<int32_t>::max()) ||
       binding.baseVertex!=int32_t(source.gpuBaseVertex))
        return fail(error,"Vulkan indirect shared staged index page or base vertex changed");
    const auto& sourcePrimitive=mesh.primitives[primitiveOrdinal];
    const auto& stagedPrimitive=staged.primitives[primitiveOrdinal];
    const uint64_t originalOffset=
        uint64_t(source.gpuFirstIndex)+sourcePrimitive.firstIndex;
    if(originalOffset>UINT32_MAX ||
       view.maxOutputClusters==0 || view.maxOutputClusters>2048 ||
       view.width<=0 || view.height<=0 ||
       !std::isfinite(view.errorThresholdPixels) ||
       view.errorThresholdPixels<=0)
        return fail(error,"Vulkan indirect invalid primitive index or camera budget");
    for(float value:view.viewProjection.m)
        if(!std::isfinite(value))
            return fail(error,"Vulkan indirect invalid projection matrix");

    GpuPassConfig safe=view;
    safe.baseVertex=int32_t(source.gpuBaseVertex);
    safe.gpuFallbackEnabled=true;
    safe.gpuFallbackFirstIndex=uint32_t(originalOffset);
    safe.gpuFallbackIndexCount=sourcePrimitive.indexCount;
    safe.gpuFallbackBaseVertex=int32_t(source.gpuBaseVertex);

    VulkanIndirectContract contract{};
    contract.source=&source;
    contract.vertex=source.vb;
    contract.index=source.ib;
    contract.geometryRevision=source.geometryRevision;
    contract.materialRevision=source.materialRevision;
    contract.expectedMaterial=stagedPrimitive.material;
    contract.primitiveOrdinal=primitiveOrdinal;
    contract.originalFirstIndex=uint32_t(originalOffset);
    contract.originalIndexCount=sourcePrimitive.indexCount;
    contract.pooledBaseVertex=int32_t(source.gpuBaseVertex);
    contract.expectedFrameSerial=device.frameSerial();
    out=contract;
    guardedView=safe;
    return true;
}

// Backwards-compatible Stage16.3 single-primitive contract. A later stage
// may widen it, but existing tests and native callers retain strict rules.
bool prepareVulkanIndirectContract(
        gfx::Device& device,const Model& source,const ModelAsset& staged,
        const GpuMetadataScene::IndexBinding& binding,
        const GpuPassConfig& view,bool hasShadowOrReflection,
        VulkanIndirectContract& out,GpuPassConfig& guardedView,
        std::string* error) {
    if(staged.primitives.size()!=1 || source.meshes.size()!=1 ||
       source.meshes[0].primitives.size()!=1)
        return fail(error,"legacy Vulkan indirect contract requires one primitive");
    return prepareVulkanIndirectPrimitiveContract(
        device,source,staged,binding,0,view,hasShadowOrReflection,
        out,guardedView,error);
}

bool validateVulkanIndirectSubmission(
        const gfx::Device& device,const Model& source,
        const VulkanIndirectContract& prepared,
        const GpuSelectionSubmission& submission,
        std::string* error) {
    if(error)error->clear();
    if(device.info().backend!=gfx::Backend::Vulkan ||
       !device.supportsCompute() || !device.gpuHealthy() ||
       !device.frameSerial() || !submission.gpuIndirectPrepared() ||
       submission.frameSerial!=device.frameSerial() ||
       submission.frameSerial!=prepared.expectedFrameSerial ||
       submission.submittedInstances!=1 ||
       submission.capacity>2048 || !prepared.source ||
       prepared.source!=&source || source.geometryRevision!=prepared.geometryRevision ||
       source.materialRevision!=prepared.materialRevision ||
       source.vb!=prepared.vertex || source.ib!=prepared.index ||
       source.gpuBaseVertex!=uint32_t(prepared.pooledBaseVertex))
        return fail(error,"Vulkan GPU indirect packet stale, incomplete or not single-instance safe");
    return true;
}
} // namespace aiverse::render::strata
