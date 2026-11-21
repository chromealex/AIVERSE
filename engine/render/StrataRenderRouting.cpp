#include "StrataRenderRouting.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error, const char* reason) {
    if (error) *error = reason;
    return false;
}
bool sourceCompatible(const Model& model, uint32_t mesh, uint32_t primitive,
                      uint32_t material, RenderPass pass) {
    if (!model.geometryPoolPage || !model.vb.valid() ||
        model.vb != model.ib || model.gpuPoolIndexBytes == 0 ||
        model.gpuPoolVertexBytes == 0 ||
        model.gpuBaseVertex > uint32_t(std::numeric_limits<int32_t>::max()) ||
        !model.skins.empty() || !model.clips.empty() ||
        !model.variants.empty() || model.morphTex.valid() ||
        model.geometry || mesh >= model.meshes.size() ||
        material >= model.materials.size())
        return false;
    const auto& definition = model.meshes[mesh];
    if (definition.deformable || !definition.lods.empty() ||
        !definition.materialBatches.empty() ||
        definition.shadowBatch.indexCount != 0 ||
        primitive >= definition.primitives.size())
        return false;
    const auto& sourcePrimitive = definition.primitives[primitive];
    if (sourcePrimitive.material != int(material) ||
        sourcePrimitive.morphTargets ||
        !sourcePrimitive.jointPalette.empty() ||
        !sourcePrimitive.variantMaterial.empty() ||
        sourcePrimitive.shadowBatched ||
        sourcePrimitive.indexCount == 0)
        return false;
    for (const auto& node : model.nodes)
        if (node.runtimeMutable || node.skin >= 0)
            return false;
    const auto& mat = model.materials[material];
    if (mat.alpha != AlphaMode::Opaque || mat.transparent ||
        mat.shaderGraph || (pass == RenderPass::SunShadow && !mat.castShadow))
        return false;
    return true;
}
} // namespace

RenderRoute chooseExclusiveRenderRoute(const RenderRouteGate& gate) {
    return gate.requested && gate.sourceAndMaterialSafe &&
           gate.sourceResident && gate.cpuGpuSelectionParityVerified &&
           gate.rasterParityVerified && gate.rendererOwnsSubmission
           ? RenderRoute::Strata : RenderRoute::Legacy;
}

bool prepareRenderCandidate(const Model& source, const ModelAsset& staged,
                            const GpuMetadataScene& gpuScene,
                            const std::string& sourceKey,
                            uint32_t mesh, uint32_t primitive,
                            RenderPass pass, const ViewConfig& view,
                            RenderCandidate& out, std::string* error) {
    if (error) error->clear();
    if (sourceKey.empty())
        return fail(error, "missing Strata source identity");
    GpuMetadataScene::IndexBinding resident;
    if (!gpuScene.indexBinding(sourceKey, source, resident) ||
        !resident.valid() || resident.buffer != source.ib ||
        resident.baseVertex != int32_t(source.gpuBaseVertex))
        return fail(error, "no matching shared-pool sidecar lease");

    size_t match = staged.primitives.size();
    uint64_t primitiveOffset = 0, totalIndices = 0;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (size_t i = 0; i < staged.primitives.size(); ++i) {
        const auto& p = staged.primitives[i];
        if (!seen.insert({p.mesh, p.primitive}).second)
            return fail(error, "duplicate Strata staged primitive identity");
        if (p.mesh == mesh && p.primitive == primitive) {
            match = i;
            primitiveOffset = totalIndices;
        }
        totalIndices += p.geometry.indices.size();
        if (totalIndices > resident.indexCount)
            return fail(error, "sidecar indices are incomplete");
    }
    if (match == staged.primitives.size() || totalIndices != resident.indexCount)
        return fail(error, "Strata primitive not found in indexed source");
    const auto& p = staged.primitives[match];
    if (!sourceCompatible(source, mesh, primitive, p.material, pass))
        return fail(error, "live source geometry or material is incompatible");

    ViewPlan planned;
    std::string selectionError;
    if (!planView(p.geometry, view, planned, &selectionError))
        return fail(error, "CPU Strata view selection failed");
    // The runtime culler uses overflow instead of CPU selection budget
    // coarsening, so a budget fallback is not a comparable renderer route.
    if (planned.budgetExceeded)
        return fail(error, "view required an unverified cluster budget fallback");
    if (planned.ranges.size() > 65536)
        return fail(error, "Strata candidate output capacity exceeded");

    RenderCandidate candidate;
    candidate.source = &source;
    candidate.sourceKey = sourceKey;
    candidate.pass = pass;
    candidate.vertexBuffer = source.vb;
    candidate.indexBuffer = resident.buffer;
    candidate.mesh = mesh;
    candidate.primitive = primitive;
    candidate.material = p.material;
    candidate.geometryRevision = source.geometryRevision;
    candidate.materialRevision = source.materialRevision;
    candidate.selectedClusters = uint32_t(planned.ranges.size());
    candidate.culledClusters = planned.culledNodes;
    candidate.ranges.reserve(planned.ranges.size());
    for (const auto& range : planned.ranges) {
        if (range.node >= p.geometry.nodes.size() ||
            range.indexCount == 0 || range.indexCount % 3 ||
            range.materialGroup != p.material ||
            range.firstIndex > p.geometry.indices.size() ||
            range.indexCount > p.geometry.indices.size() - range.firstIndex)
            return fail(error, "invalid selected Strata cluster range");
        const uint64_t base = uint64_t(resident.firstIndex) +
                              primitiveOffset + range.firstIndex;
        const uint64_t last = base + range.indexCount;
        const uint64_t leaseEnd = uint64_t(resident.firstIndex) +
                                  resident.indexCount;
        if (base > std::numeric_limits<uint32_t>::max() ||
            last > leaseEnd ||
            last > std::numeric_limits<uint32_t>::max())
            return fail(error, "Strata cluster range escapes shared GPU index lease");
        candidate.ranges.push_back({
            uint32_t(base), range.indexCount, resident.baseVertex,
            range.materialGroup, range.node
        });
    }
    out = std::move(candidate);
    return true;
}

bool encodeRendererCommands(const Model& liveSource,
                            const GpuMetadataScene& liveResidency,
                            const RenderCandidate& candidate,
                            uint32_t rendererInstanceSlot,
                            const RenderRouteGate& gate,
                            std::vector<gfx::DrawIndexedIndirectCommand>& out) {
    if (chooseExclusiveRenderRoute(gate) != RenderRoute::Strata ||
        rendererInstanceSlot == invalidNode ||
        candidate.source != &liveSource ||
        !liveSource.geometryPoolPage ||
        candidate.geometryRevision != liveSource.geometryRevision ||
        candidate.materialRevision != liveSource.materialRevision ||
        candidate.vertexBuffer != liveSource.vb ||
        candidate.indexBuffer != liveSource.ib ||
        !candidate.vertexBuffer.valid() || !candidate.indexBuffer.valid() ||
        !sourceCompatible(liveSource, candidate.mesh, candidate.primitive,
                          candidate.material, candidate.pass))
        return false;
    GpuMetadataScene::IndexBinding binding;
    if (!liveResidency.indexBinding(candidate.sourceKey, liveSource, binding) ||
        !binding.valid() || binding.buffer != candidate.indexBuffer ||
        binding.baseVertex != int32_t(liveSource.gpuBaseVertex))
        return false;
    std::vector<gfx::DrawIndexedIndirectCommand> commands;
    commands.reserve(candidate.ranges.size());
    for (const auto& range : candidate.ranges) {
        if (!range.indexCount || range.indexCount % 3 ||
            range.material != candidate.material ||
            range.baseVertex != binding.baseVertex ||
            range.firstIndex < binding.firstIndex ||
            uint64_t(range.firstIndex) + range.indexCount >
                uint64_t(binding.firstIndex) + binding.indexCount ||
            range.firstIndex > std::numeric_limits<uint32_t>::max() -
                               range.indexCount)
            return false;
        gfx::DrawIndexedIndirectCommand command;
        command.indexCount = range.indexCount;
        command.instanceCount = 1;
        command.firstIndex = range.firstIndex; // absolute shared-pool index
        command.baseVertex = range.baseVertex;
        command.firstInstance = rendererInstanceSlot; // renderer owns uniforms
        commands.push_back(command);
    }
    out = std::move(commands);
    return true;
}
} // namespace aiverse::render::strata
