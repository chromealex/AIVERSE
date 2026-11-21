#pragma once
// STRATA Stage 16.2: strict CPU preflight for future production consumption
// of Vulkan GPU-authored indexed indirect arguments. Does not draw anything.
// Declines multi-material, animated, morphed, multiple-transform and shadow/
// reflection-dependent submissions; Renderer3D keeps Legacy by default.
#include "StrataGpuCulling.h"

namespace aiverse::render::strata {
struct VulkanIndirectContract {
    const Model* source=nullptr; // borrowed; source must survive GPU submit
    gfx::BufferHandle vertex, index;
    uint64_t geometryRevision=0, materialRevision=0;
    uint32_t expectedMaterial=0;
    uint32_t primitiveOrdinal=0;
    uint32_t originalFirstIndex=0, originalIndexCount=0;
    int32_t pooledBaseVertex=0;
    uint64_t expectedFrameSerial=0;
};

// Stage 16.4a: one GPU submission per primitive and per scene instance.
// A single Model can have up to 16 distinct opaque primitives and materials.
// Each submission inherits its OWN material/transform U0 from Renderer3D,
// never a material ID from an arbitrary GPU atomic output ordering.
// All selected primitives must match the original uploaded pooled geometry.
bool prepareVulkanIndirectPrimitiveContract(
    gfx::Device& device,const Model& source,const ModelAsset& staged,
    const GpuMetadataScene::IndexBinding& binding,uint32_t primitiveOrdinal,
    const GpuPassConfig& view,bool hasShadowOrReflection,
    VulkanIndirectContract& out,GpuPassConfig& guardedView,
    std::string* error=nullptr);

// Preconditions: one rigid opaque material/primitive/node, shared pooled GPU
// VB/IB, no morph/skin/author LOD, and one main camera (no shadow/mirror).
// Writes GpuPassConfig fallback atomically so an error never leaves a
// seemingly enabled indirect packet. Does not inspect GPU buffer contents.
bool prepareVulkanIndirectContract(
    gfx::Device& device,const Model& source,const ModelAsset& staged,
    const GpuMetadataScene::IndexBinding& binding,
    const GpuPassConfig& view,bool hasShadowOrReflection,
    VulkanIndirectContract& out,GpuPassConfig& guardedView,
    std::string* error=nullptr);

// Call after GpuClusterCulling::dispatch, before ever considering
// Renderer3D indexed indirect submission. Verifies source identity,
// frame serial and RHI handles; it CANNOT prove shader GPU execution.
// Production must additionally show main-view pixel parity first.
bool validateVulkanIndirectSubmission(
    const gfx::Device& device,const Model& source,
    const VulkanIndirectContract& prepared,
    const GpuSelectionSubmission& submission,
    std::string* error=nullptr);
} // namespace aiverse::render::strata
