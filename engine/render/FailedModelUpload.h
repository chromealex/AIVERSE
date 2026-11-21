// Genre-neutral fail-closed cleanup for a directly uploaded Model.
//
// ModelResourceCache owners should instead use retireUnpublished(), which
// batches models behind genuine GPU completion tokens without blocking.
// Small native tools and direct geometry hosts without such an owner may
// call this only when uploadModel() returned false or threw AFTER starting
// GPU allocation. It is a failure path, not ordinary per-frame eviction.
#pragma once
#include "Model.h"
#include <memory>

namespace aiverse::render {

enum class FailedModelUploadDisposition {
    NoModel,
    ReleasedAfterGpuIdle,
    OrphanedWithoutGpuProof
};

// The host guarantees the render-owner thread and an alive gfx::Device.
// A partially uploaded Model may hold vertex/index/image/morph handles.
// Never let unique_ptr destroy it until the GPU proves those handles idle.
// If even that completion check fails, intentionally abandon CPU ownership
// rather than expose GPU-visible memory to premature destruction.
inline FailedModelUploadDisposition abortFailedModelUpload(
        gfx::Device& device,std::unique_ptr<Model>& model) noexcept {
    if (!model) return FailedModelUploadDisposition::NoModel;
    try {
        if (device.waitForResourceIdle()) {
            releaseModel(device,*model);
            model.reset();
            return FailedModelUploadDisposition::ReleasedAfterGpuIdle;
        }
    } catch (...) {
        // A failed/destroyed backend is not a GPU completion signal.
    }
    (void)model.release();
    return FailedModelUploadDisposition::OrphanedWithoutGpuProof;
}

} // namespace aiverse::render
