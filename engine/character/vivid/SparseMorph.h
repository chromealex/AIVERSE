// VIVID unbounded (per asset) sparse facial morph data and portable CPU reference.
// Does not change legacy render::Model layout or install an RHI backend.
// GPU buffer layout is ABI-frozen: 16-byte aligned std430/Metal-compatible records.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::vivid {

using MorphVec3 = std::array<float, 3>;
struct MorphVertex {
    MorphVec3 position{};
    MorphVec3 normal{};
    MorphVec3 tangent{};
};
struct MorphDelta {
    std::uint32_t vertex = 0;
    MorphVec3 position{};
    MorphVec3 normal{};
    MorphVec3 tangent{};
};
struct MorphTargetInput {
    std::string name;
    std::vector<MorphDelta> deltas;
};

// Immutable binary GPU buffer layout. Explicit padding; no compiler-dependent
// vec3 alignment. In GLSL use uint+3 uint padding and three vec4; in MSL
// similarly align to 16 bytes. The .w components are zero/reserved.
struct alignas(16) GpuMorphDelta {
    std::uint32_t vertex = 0;
    std::uint32_t reserved[3] = {};
    float position[4] = {};
    float normal[4] = {};
    float tangent[4] = {};
};
static_assert(sizeof(GpuMorphDelta) == 64);
static_assert(alignof(GpuMorphDelta) == 16);
static_assert(offsetof(GpuMorphDelta, position) == 16);
static_assert(offsetof(GpuMorphDelta, normal) == 32);
static_assert(offsetof(GpuMorphDelta, tangent) == 48);

// [first, first+count) inside a shared delta array. Shader storage buffer can
// upload these trivially. Only the CPU owns UTF-8 target names.
struct alignas(16) GpuMorphRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t reserved[2] = {};
};
static_assert(sizeof(GpuMorphRange) == 16);
static_assert(alignof(GpuMorphRange) == 16);

// CSR gather: offsets[v]..offsets[v+1] holds links for a single vertex.
// Each GPU vertex/thread owns its output, therefore no float atomics.
struct GpuMorphLink {
    std::uint32_t target = 0; // index into target weight array
    std::uint32_t delta = 0;  // index into shared GpuMorphDelta array
};
static_assert(sizeof(GpuMorphLink) == 8);

class SparseMorphLibrary {
public:
    // Validates names/vertices/finite deltas, canonicalizes each target to
    // ascending vertex IDs and rejects duplicate vertices or target names.
    // Zero-valued deltas are discarded. No hard eight-target cap.
    // Throws std::invalid_argument/length_error on malformed input.
    static SparseMorphLibrary compile(std::size_t vertexCount, std::vector<MorphTargetInput> targets);

    std::size_t vertices() const noexcept { return vertices_; }
    std::size_t targets() const noexcept { return names_.size(); }
    std::size_t deltas() const noexcept { return deltas_.size(); }
    std::size_t bufferBytes() const noexcept {
        return deltas_.size() * sizeof(GpuMorphDelta)
            + ranges_.size() * sizeof(GpuMorphRange)
            + offsets_.size() * sizeof(std::uint32_t)
            + links_.size() * sizeof(GpuMorphLink);
    }
    std::span<const GpuMorphDelta> gpuDeltas() const noexcept { return deltas_; }
    std::span<const GpuMorphRange> gpuRanges() const noexcept { return ranges_; }
    // RHI-neutral CSR storage. offsets count = vertexCount + 1, links count
    // = total nonzero deltas, ordered by target within each vertex.
    std::span<const std::uint32_t> gpuVertexOffsets() const noexcept { return offsets_; }
    std::span<const GpuMorphLink> gpuVertexLinks() const noexcept { return links_; }
    std::string_view targetName(std::size_t i) const noexcept {
        return i < names_.size() ? std::string_view{names_[i]} : std::string_view{};
    }
    bool findTarget(std::string_view name, std::size_t& index) const noexcept;

    // Portable reference for numerical comparison against Metal/GL/Vulkan.
    // 'base' and 'output' must be distinct or precisely the same span.
    // Each weight must be finite in [0,1], with one weight per target.
    // Returns false without writing output if any input, size or base value
    // is invalid. Optional normal/tangent normalization occurs after all
    // weighted deltas. No heap allocations or GPU calls per invocation.
    bool evaluate(std::span<const MorphVertex> base, std::span<const float> weights,
                  std::span<MorphVertex> output, bool normalizeDirections = true) const noexcept;
    // GPU-style per-vertex gather reference. Same numerical result as evaluate()
    // within floating-point tolerance; no vertex scatter/atomic operations.
    bool evaluateGather(std::span<const MorphVertex> base, std::span<const float> weights,
                        std::span<MorphVertex> output,
                        bool normalizeDirections = true) const noexcept;

private:
    std::size_t vertices_ = 0;
    std::vector<std::string> names_;
    std::vector<GpuMorphRange> ranges_;
    std::vector<GpuMorphDelta> deltas_;
    std::vector<std::uint32_t> offsets_;
    std::vector<GpuMorphLink> links_;
};

} // namespace aiverse::vivid
