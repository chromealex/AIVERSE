// VIVID opt-in GPU sparse morph execution using the existing AIVERSE gfx::Device.
// Platform-neutral owner of RHI compute resources; no direct Metal/Vulkan calls.
#pragma once
#include "SparseMorph.h"
#include "engine/gfx/Gfx.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {

struct alignas(16) GpuMorphVertex {
    float position[4] = {};
    float normal[4] = {};
    float tangent[4] = {};
};
static_assert(sizeof(GpuMorphVertex) == 48);
static_assert(alignof(GpuMorphVertex) == 16);
static_assert(offsetof(GpuMorphVertex, position) == 0);
static_assert(offsetof(GpuMorphVertex, normal) == 16);
static_assert(offsetof(GpuMorphVertex, tangent) == 32);

// std140 (GLSL) / constant buffer (MSL) matching offset/size, slots documented
// in VividComputeShaders.h.
struct alignas(16) GpuMorphUniforms {
    std::uint32_t vertices = 0;
    std::uint32_t targets = 0;
    std::uint32_t deltas = 0;
    std::uint32_t links = 0;
    std::uint32_t normalizeDirections = 1;
    std::uint32_t reserved[3] = {};
};
static_assert(sizeof(GpuMorphUniforms) == 32);
static_assert(alignof(GpuMorphUniforms) == 16);

// Reference packing uses float4 streams, not the 36-byte CPU MorphVertex ABI.
GpuMorphVertex packMorphVertex(const MorphVertex& src) noexcept;
MorphVertex unpackMorphVertex(const GpuMorphVertex& src) noexcept;

class GpuMorphDeformer {
public:
    GpuMorphDeformer() = default;
    ~GpuMorphDeformer() { release(); }
    GpuMorphDeformer(const GpuMorphDeformer&) = delete;
    GpuMorphDeformer& operator=(const GpuMorphDeformer&) = delete;
    // Device must outlive this resource owner; release before device teardown.

    // Must be called on the gfx device thread, outside an active render pass.
    // Asset/vertex data is uploaded once; reuse the instance for many updates.
    // Returns false on invalid input, backend without compute or failed GPU
    // allocation/compilation; the caller may fall back to CPU evaluateGather.
    bool initialize(gfx::Device& device, const SparseMorphLibrary& library,
                    std::span<const MorphVertex> baseVertices);
    // Legal only after beginFrame and before endFrame, outside render passes.
    // Invalid weights are rejected without updating GPU resources.
    bool dispatch(std::span<const float> weights, bool normalizeDirections = true);
    // Diagnostics only: synchronous readback between frames, never on hot path.
    bool download(std::span<MorphVertex> output) const;
    void release() noexcept;
    bool ready() const noexcept { return device_ && compute_.valid(); }
    std::size_t vertexCount() const noexcept { return vertexCount_; }
    std::size_t targetCount() const noexcept { return targetCount_; }
    std::size_t gpuBytes() const noexcept { return gpuBytes_; }
    gfx::BufferHandle outputBuffer() const noexcept { return buffers_[5]; }
    const std::string& lastError() const noexcept { return error_; }

private:
    gfx::Device* device_ = nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle buffers_[6]{};
    std::size_t vertexCount_ = 0;
    std::size_t targetCount_ = 0;
    std::size_t gpuBytes_ = 0;
    GpuMorphUniforms uniforms_{};
    std::string error_;
};

} // namespace aiverse::vivid
