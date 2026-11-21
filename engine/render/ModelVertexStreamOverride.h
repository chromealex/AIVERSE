// Render-side compatibility gate for opt-in compute-generated ModelVertex
// streams (e.g. VIVID). No dependency on character animation or any genre.
#pragma once

#include "Model.h"
#include <cstdint>
#include <limits>

namespace aiverse::render {

enum class ModelVertexOverrideStatus : std::uint8_t {
    Disabled,
    Ready,
    MissingBuffer,
    InvalidVertexCount,
    UnsupportedSharedGeometry,
    UnsupportedGeometryData
};

// A compute-generated stream must contain the FULL 60B indexed ModelVertex
// layout. Independent meshes/LODs share one per-model vertex index space.
// Do not use an override with a pooled/base-vertex draw; the provided buffer
// is a dedicated model-relative stream, not the global geometry pool.
inline ModelVertexOverrideStatus modelVertexOverrideStatus(
        const Model& model, gfx::BufferHandle buffer,
        std::uint32_t vertexCount) noexcept {
    if (!buffer.valid())
        return vertexCount ? ModelVertexOverrideStatus::MissingBuffer
                           : ModelVertexOverrideStatus::Disabled;
    if (!vertexCount || !model.vb.valid() || !model.ib.valid())
        return ModelVertexOverrideStatus::InvalidVertexCount;
    if (model.geometryPoolPage || model.gpuBaseVertex || model.gpuFirstIndex)
        return ModelVertexOverrideStatus::UnsupportedSharedGeometry;
    if (model.geometry)
        return ModelVertexOverrideStatus::UnsupportedGeometryData;
    std::uint64_t end = 0;
    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (!prim.vertexCount || !prim.indexCount) continue;
            const auto last = std::uint64_t(prim.firstVertex) + prim.vertexCount;
            if (last > std::numeric_limits<std::uint32_t>::max())
                return ModelVertexOverrideStatus::InvalidVertexCount;
            if (last > end) end = last;
        }
    }
    // A model may still retain its original CPU vertices before upload. In
    // that case validate exact size; after upload the primitive index ranges
    // provide the strongest available count without retaining CPU geometry.
    if (!end || vertexCount < end ||
        (!model.vertices.empty() && model.vertices.size() != vertexCount))
        return ModelVertexOverrideStatus::InvalidVertexCount;
    return ModelVertexOverrideStatus::Ready;
}

} // namespace aiverse::render
