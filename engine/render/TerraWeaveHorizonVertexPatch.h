// TerraWeave Horizon: conservative GPU vertex range updates for
// unchanged index/cluster topology. Shared with all native genres.
// No GL/Metal/Vulkan code and NO writing into global pooled geometry.
#pragma once
#include "engine/render/TerraWeaveHorizonGpuPatch.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace aiverse::terraweave {

struct HorizonVertexUploadRange {
    uint32_t firstVertex = 0, vertexCount = 0;
};
struct HorizonVertexUploadPlan {
    std::vector<HorizonVertexUploadRange> ranges;
    size_t changedVertices = 0, updatedBytes = 0;
};

inline bool horizonBoundsContain(render::float3 outerMin,
                                render::float3 outerMax,
                                render::float3 innerMin,
                                render::float3 innerMax) {
    return innerMin.x >= outerMin.x && innerMin.y >= outerMin.y &&
           innerMin.z >= outerMin.z && innerMax.x <= outerMax.x &&
           innerMax.y <= outerMax.y && innerMax.z <= outerMax.z;
}

// Only the terrain vertex PREFIX may differ. Existing pooled/indirect
// model layouts, card topology, and all bounds must remain valid.
// This pure check does not access the device or mutate the resident model.
inline std::optional<HorizonVertexUploadPlan> planHorizonVertexUpload(
        const HorizonBake& before, const HorizonBake& after,
        const render::Model& resident,
        std::span<const render::ModelVertex> residentVertices,
        std::span<const uint32_t> residentIndices,
        const render::Model& prepared,
        size_t maxRanges = 20, uint32_t maxPercent = 35) {
    static_assert(std::is_trivially_copyable_v<render::ModelVertex>);
    if (!horizonCanReuseHorizonLayout(before, after) ||
        maxRanges == 0 || maxRanges > 256 ||
        maxPercent == 0 || maxPercent > 100 ||
        resident.geometryPoolPage || resident.gpuBaseVertex != 0 ||
        resident.gpuFirstIndex != 0 || !resident.vb.valid() ||
        !resident.ib.valid() || resident.batching.allowGeometryPool ||
        residentVertices.size() != prepared.vertices.size() ||
        residentIndices.size() != prepared.indices.size() ||
        residentVertices.empty() ||
        before.terrainVertices.size() > residentVertices.size() ||
        resident.meshes.size() != prepared.meshes.size() ||
        resident.nodes.size() != prepared.nodes.size() ||
        resident.materials.size() != prepared.materials.size() ||
        !horizonBoundsContain(resident.boundsMin, resident.boundsMax,
                              prepared.boundsMin, prepared.boundsMax) ||
        !std::equal(residentIndices.begin(), residentIndices.end(),
                    prepared.indices.begin()))
        return std::nullopt;

    // An index-compatible model can still have different primitive ownership
    // or per-node transforms. Those changes must use a full upload.
    for (size_t i = 0; i < resident.meshes.size(); ++i) {
        const auto& a = resident.meshes[i], &b = prepared.meshes[i];
        if (a.primitives.size() != b.primitives.size() ||
            !horizonBoundsContain(a.boundsMin, a.boundsMax,
                                  b.boundsMin, b.boundsMax))
            return std::nullopt;
        for (size_t j = 0; j < a.primitives.size(); ++j) {
            const auto& p = a.primitives[j], &q = b.primitives[j];
            if (p.firstVertex != q.firstVertex ||
                p.vertexCount != q.vertexCount ||
                p.firstIndex != q.firstIndex ||
                p.indexCount != q.indexCount ||
                p.material != q.material ||
                !horizonBoundsContain(p.boundsMin,p.boundsMax,
                                      q.boundsMin,q.boundsMax))
                return std::nullopt;
        }
    }
    for (size_t i = 0; i < resident.nodes.size(); ++i)
        if (resident.nodes[i].mesh != prepared.nodes[i].mesh ||
            resident.nodes[i].parent != prepared.nodes[i].parent)
            return std::nullopt;

    const auto different = [&](size_t index) {
        return std::memcmp(&residentVertices[index],
                           &prepared.vertices[index],
                           sizeof(render::ModelVertex)) != 0;
    };
    const size_t terrainCount = before.terrainVertices.size();
    // All other vertices are forest-crosscard geometry. Updating their
    // ranges would also require a dynamic bounds/layout policy.
    for (size_t i = terrainCount; i < residentVertices.size(); ++i)
        if (different(i)) return std::nullopt;

    HorizonVertexUploadPlan result;
    for (size_t i = 0; i < terrainCount;) {
        if (!different(i)) { ++i; continue; }
        const size_t begin = i;
        while (i < terrainCount && different(i)) ++i;
        const size_t count = i - begin;
        if (begin > std::numeric_limits<uint32_t>::max() ||
            count > std::numeric_limits<uint32_t>::max())
            return std::nullopt;
        result.changedVertices += count;
        result.ranges.push_back({
            static_cast<uint32_t>(begin),static_cast<uint32_t>(count)});
        if (result.ranges.size() > maxRanges)
            return std::nullopt;
    }
    result.updatedBytes = result.changedVertices *
                          sizeof(render::ModelVertex);
    if (result.changedVertices * 100 > terrainCount * maxPercent)
        return std::nullopt;
    return result;
}

inline bool horizonCanApplyVertexUpload(
        const gfx::Device& device, const render::Model& resident,
        const render::Model& updated, const HorizonVertexUploadPlan& plan) {
    if (!device.canUpdateSharedBuffers() ||
        !resident.vb.valid() || !resident.ib.valid() ||
        resident.geometryPoolPage || resident.gpuBaseVertex ||
        resident.gpuFirstIndex || resident.batching.allowGeometryPool ||
        updated.vertices.empty())
        return false;
    const size_t total = updated.vertices.size();
    for (const auto& r : plan.ranges)
        if (!r.vertexCount ||
            r.firstVertex > total ||
            r.vertexCount > total - r.firstVertex)
            return false;
    return true;
}

// Render-owner thread only. The RHI's updateBuffer does not return success;
// the planner cannot detect a device-loss failure. Caller must use the
// existing full-upload path when preflight fails and retain old CPU snapshots
// until the entire new representation is committed.
inline bool applyHorizonVertexUpload(
        gfx::Device& device, render::Model& resident,
        const render::Model& updated, const HorizonVertexUploadPlan& plan) {
    if (!horizonCanApplyVertexUpload(device,resident,updated,plan))
        return false;
    for (const auto& r : plan.ranges) {
        const size_t offset = size_t(r.firstVertex) *
                              sizeof(render::ModelVertex);
        const size_t bytes = size_t(r.vertexCount) *
                             sizeof(render::ModelVertex);
        device.updateBuffer(resident.vb,
                            updated.vertices.data() + r.firstVertex,
                            bytes, offset);
    }
    return true;
}

} // namespace aiverse::terraweave
