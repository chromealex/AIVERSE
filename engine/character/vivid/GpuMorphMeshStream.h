// Convert VIVID's 48-byte sparse morph GPU output into the existing 60-byte
// render::ModelVertex layout. Source attributes are transferred bit-for-bit.
// No Model ABI changes, no duplicate MotionWeave skinning or CPU readback.
#pragma once
#include "GpuMorph.h"
#include "engine/render/Model.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace aiverse::vivid {

// Shader contract: 15 uint32 words per ModelVertex, input/output buffer slots
// 0/2. Slot 1 is the GpuMorphVertex (3 float4 per vertex). Uniform slot 8.
struct alignas(16) GpuMorphMeshParams {
    std::uint32_t vertexCount = 0;
    std::uint32_t reserved[3] = {};
};
static_assert(sizeof(GpuMorphMeshParams) == 16);

static_assert(sizeof(render::ModelVertex) == 60);
static_assert(offsetof(render::ModelVertex, pos) == 0);
static_assert(offsetof(render::ModelVertex, nrm) == 12);
static_assert(offsetof(render::ModelVertex, uv) == 24);
static_assert(offsetof(render::ModelVertex, tangent) == 32);
static_assert(offsetof(render::ModelVertex, color) == 48);
static_assert(offsetof(render::ModelVertex, joints) == 52);
static_assert(offsetof(render::ModelVertex, weights) == 56);

// Device-thread, opt-in adapter. Must release before gfx::Device destruction.
// The returned Storage buffer has the *same vertex bytes and index space* as
// render::ModelVertex and is suitable for a future custom model draw override.
// Do NOT substitute this buffer into Renderer3D until the render pass/culling
// ownership and shadow/material parity gates are implemented.
class GpuMorphMeshStream {
public:
    GpuMorphMeshStream() = default;
    ~GpuMorphMeshStream() { release(); }
    GpuMorphMeshStream(const GpuMorphMeshStream&) = delete;
    GpuMorphMeshStream& operator=(const GpuMorphMeshStream&) = delete;

    // Full model vertex stream, not a single primitive's local indices.
    bool initialize(gfx::Device& device, std::span<const render::ModelVertex> vertices);
    // Call after GpuMorphDeformer::dispatch(), within the same GPU frame and
    // outside render passes. The deformed output must match vertexCount().
    bool dispatch(gfx::BufferHandle deformed, std::size_t deformedVertexCount);
    // Diagnostic readback only, between frames.
    bool download(std::span<render::ModelVertex> output) const;
    void release() noexcept;

    bool ready() const noexcept { return device_ && compute_.valid(); }
    std::size_t vertexCount() const noexcept { return vertexCount_; }
    gfx::BufferHandle vertexBuffer() const noexcept { return output_; }
    const std::string& lastError() const noexcept { return error_; }

private:
    gfx::Device* device_ = nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle original_{}, output_{};
    std::size_t vertexCount_ = 0;
    GpuMorphMeshParams params_{};
    std::string error_;
};

} // namespace aiverse::vivid
