// Immutable GPU morph asset shared by VIVID character instances.
// Owns uploaded bind-pose positions, sparse CSR, original 60B ModelVertex
// data, and the two GPU compute programs. Does NOT own per-instance weights,
// deformation output or any skeletal animation (MotionWeave owns those).
#pragma once

#include "GpuMorph.h"
#include "GpuMorphMeshStream.h"
#include "engine/render/Model.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace aiverse::vivid {

class GpuMorphSharedAsset final {
public:
    static std::shared_ptr<GpuMorphSharedAsset> create(
        gfx::Device& device, const SparseMorphLibrary& library,
        std::span<const render::ModelVertex> original,
        std::string* error = nullptr);

    ~GpuMorphSharedAsset() { release(); }
    GpuMorphSharedAsset(const GpuMorphSharedAsset&) = delete;
    GpuMorphSharedAsset& operator=(const GpuMorphSharedAsset&) = delete;
    bool ready() const noexcept;
    gfx::Device* device() const noexcept { return device_; }
    std::size_t vertexCount() const noexcept { return vertexCount_; }
    std::size_t targetCount() const noexcept { return targetCount_; }
    std::size_t gpuBytes() const noexcept { return gpuBytes_; }
    const std::string& lastError() const noexcept { return error_; }

    // Internal compute handles are borrowed. Actors hold shared_ptr until
    // their own GPU completion marker confirms all uses have finished.
    gfx::ComputeHandle morphCompute() const noexcept { return morphKernel_; }
    gfx::ComputeHandle meshCompute() const noexcept { return packKernel_; }
    gfx::BufferHandle morphInput(std::size_t slot) const noexcept {
        return slot < 4 ? data_[slot] : gfx::BufferHandle{};
    }
    gfx::BufferHandle originalModelVertices() const noexcept { return data_[4]; }
    GpuMorphUniforms morphUniforms(bool normalize) const noexcept;
    GpuMorphMeshParams meshUniforms() const noexcept;

private:
    GpuMorphSharedAsset() = default;
    bool initialize(gfx::Device&,const SparseMorphLibrary&,
                    std::span<const render::ModelVertex>);
    void release() noexcept;

    gfx::Device* device_ = nullptr;
    std::array<gfx::BufferHandle,5> data_{};
    gfx::ComputeHandle morphKernel_{}, packKernel_{};
    std::size_t vertexCount_ = 0, targetCount_ = 0, gpuBytes_ = 0;
    GpuMorphUniforms params_{};
    std::string error_;
};

} // namespace aiverse::vivid
