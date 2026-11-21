// VIVID per-character GPU presentation output + semantic facial weights.
// Shared GPU morph inputs/programs are owned by GpuMorphSharedAsset, not
// duplicated by each actor. MotionWeave remains the sole skeleton system.
#pragma once

#include "FaceShapeBinding.h"
#include "GpuMorphSharedAsset.h"
#include "engine/render/ModelVertexStreamOverride.h"
#include "engine/render/Renderer3D.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {

class GpuActorStream final {
public:
    GpuActorStream() = default;
    ~GpuActorStream() = default;
    GpuActorStream(const GpuActorStream&) = delete;
    GpuActorStream& operator=(const GpuActorStream&) = delete;

    // Backward-compatible isolated asset initializer: private immutable
    // GPU inputs are created once for this particular actor only.
    bool initialize(gfx::Device& device, const render::Model& uploadedModel,
                    const SparseMorphLibrary& library,
                    std::span<const render::ModelVertex> original);

    // Scalable initializer: different actors hold the SAME shared GPU asset
    // and pipelines but have independent weights and deformed 48B/60B
    // writable output buffers. Shared asset must describe uploadedModel's
    // full model-relative vertex stream, with identical vertex ordering.
    bool initializeShared(gfx::Device& device,
                          const render::Model& uploadedModel,
                          std::shared_ptr<GpuMorphSharedAsset> sharedAsset);

    // One two-stage GPU dispatch per actor per gfx frame. Both compute passes
    // happen after beginFrame and before any render passes. No CPU readback.
    bool dispatch(const FaceFrame& face, const FaceShapeBinding& mapping,
                  float influence = 1.0f, bool normalizeDirections = true);

    // Only attaches this actor's current-frame 60B buffer to its own Model.
    // MotionWeave's final bone matrices are supplied by the caller via
    // draw.nodeWorld; VIVID never schedules skeleton animation.
    bool attach(render::ModelDraw& draw) const noexcept;
    bool download(std::span<render::ModelVertex> vertices) const;

    // Synchronous rare scene teardown: call at a host frame boundary after
    // all GPU drawing is committed. For frequent despawns prefer the shared
    // GpuActorRetirementQueue completion-token path.
    bool shutdownAfterIdle();
    bool ready() const noexcept {
        return device_ && shared_ && shared_->ready() &&
               weightsBuffer_.valid() && morphOutput_.valid() &&
               packedOutput_.valid();
    }
    std::size_t vertexCount() const noexcept {
        return shared_ ? shared_->vertexCount() : 0;
    }
    std::size_t targetCount() const noexcept {
        return shared_ ? shared_->targetCount() : 0;
    }
    std::uint64_t dispatchedFrame() const noexcept { return lastCompletedFrame_; }
    gfx::BufferHandle vertexBuffer() const noexcept { return packedOutput_; }
    const GpuMorphSharedAsset* sharedAsset() const noexcept { return shared_.get(); }
    const std::string& lastError() const noexcept { return error_; }

private:
    friend class GpuActorRetirementQueue;
    friend class HumanRetirementQueue;
    // Only a successful GPU completion token or device-wide idle barrier
    // authorizes destroying this actor's output/weights or final asset ref.
    void releaseAfterGpuCompletion() noexcept;

    gfx::Device* device_ = nullptr;
    const render::Model* model_ = nullptr;
    std::shared_ptr<GpuMorphSharedAsset> shared_;
    gfx::BufferHandle weightsBuffer_{}, morphOutput_{}, packedOutput_{};
    std::vector<float> weights_;
    std::uint64_t lastAttemptedFrame_ = 0;
    std::uint64_t lastCompletedFrame_ = 0;
    std::string error_;
};

} // namespace aiverse::vivid
