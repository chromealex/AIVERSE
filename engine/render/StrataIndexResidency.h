#pragma once
// STRATA Stage 15: budgeted index-only GPU residency, shared by all genres.
// Source Model VB remains owned by Renderer3D / GlobalGeometryPool.
// Residency is one immutable, compact indexed RHI buffer generation; never
// overwrite GPU indices while an earlier frame may still read them.
// Call updates at a renderer submission epoch boundary, before drawModel().
#include "StrataModelAdapter.h"
#include "StrataDiskPages.h"
#include "../gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::render::strata {
// Pure load-time aggregate budget arbitration. Each request already has
// its per-source cap applied; sources that cannot pin roots are declined.
// A sceneBudget of zero grants each compatible source its cap.
struct SceneIndexBudgetRequest {
    std::string key;
    uint64_t rootBytes = 0, maxBytes = 0;
};
struct SceneIndexBudgetGrant {
    std::string key;
    uint64_t bytes = 0; // zero => keep this source on Legacy
};
std::vector<SceneIndexBudgetGrant> apportionSceneIndexBudget(
    std::vector<SceneIndexBudgetRequest> requests,
    uint64_t sceneBudgetBytes);

struct IndexDemand {
    uint32_t primitive = 0; // ordinal in ModelAsset::primitives
    uint32_t node = 0;
    float priority = 0.0f; // larger demand wins when budget is exhausted
};
struct IndexSpan {
    uint32_t firstIndex = 0, indexCount = 0;
    bool valid() const { return indexCount != 0; }
};
struct IndexResidencyStats {
    uint64_t activeBytes = 0, pinnedRootBytes = 0, budgetBytes = 0;
    // Stage 15.2: the arena is a fixed GPU allocation, activeBytes counts
    // live pages, arenaBytes counts its full reserved capacity. Vulkan RHI
    // may still retain multiple device versions and a CPU shadow.
    uint64_t arenaBytes = 0, uploadedBytes = 0, uploadCalls = 0;
    // Checked RHI transfer bytes, including successfully written orphan
    // slots from a later failed all-sibling transaction. This counter,
    // not published uploadedBytes, enforces the per-frame aggregate cap.
    uint64_t gpuTransferBytes = 0;
    uint64_t uploadBudgetDeferrals = 0, fenceReuseDeferrals = 0;
    bool incremental = false;
    // Background archive reads; bytes are verified CPU pages, not GPU DMA.
    uint64_t diskBytesRead = 0, diskCompletedReads = 0;
    uint64_t diskFailures = 0, diskPending = 0, diskReadyBytes = 0;
    uint64_t generations = 0, uploads = 0, evictions = 0;
    uint64_t deferredDemands = 0, failedUploads = 0;
    uint32_t residentNodes = 0;
};
// Stage18.2: difference of monotonic source counters before/after one
// residency attempt. Failed attempts can still transfer orphan GPU bytes.
// This is a non-mutating CPU telemetry helper, independent of backend.
struct IndexResidencyFrameDelta {
    uint64_t gpuTransferBytes=0, uploadedBytes=0, promotions=0;
    uint64_t evictions=0, deferredDemands=0, failedUploads=0;
};
inline IndexResidencyFrameDelta indexResidencyFrameDelta(
    const IndexResidencyStats& before,const IndexResidencyStats& after) {
    const auto delta=[](uint64_t newer,uint64_t older) -> uint64_t {
        return newer>=older ? newer-older : 0;
    };
    return {
        delta(after.gpuTransferBytes,before.gpuTransferBytes),
        delta(after.uploadedBytes,before.uploadedBytes),
        delta(after.uploads,before.uploads),
        delta(after.evictions,before.evictions),
        delta(after.deferredDemands,before.deferredDemands),
        delta(after.failedUploads,before.failedUploads)
    };
}

class IndexResidency final {
public:
    IndexResidency(); // out-of-line: owns incomplete IncrementalStorage
    IndexResidency(const IndexResidency&) = delete;
    IndexResidency& operator=(const IndexResidency&) = delete;
    ~IndexResidency(); // call shutdown(device) while RHI is alive

    // Performs a real root-only index upload, not a virtual residency flag.
    // Roots are pinned; if budget cannot contain them, returns false without
    // allocating/publishing any GPU state.
    bool initialize(gfx::Device& device, const ModelAsset& asset,
                    size_t budgetBytes, std::string* error = nullptr,
                    bool preferIncremental = false,
                    size_t uploadBudgetBytes = 0,
                    std::shared_ptr<DiskIndexPages> diskPages = {});
    bool incrementalEnabled() const { return bool(incremental_); }
    // Atomic compact/repack: requests promote complete sibling groups along
    // each path (parent->children). Rejected requests retain a drawable
    // resident ancestor. Existing generation is unchanged on GPU failure.
    // Can evict unrequested descendants; never evicts pinned roots.
    // Additional per-call cap applies to incremental GPU writes only. Full
    // immutable GLES3 fallback preserves its existing atomic swap semantics.
    bool update(const std::vector<IndexDemand>& demands,
                std::string* error = nullptr,
                size_t uploadAllowanceBytes = SIZE_MAX);

    IndexSpan span(uint32_t primitive, uint32_t node) const;
    bool resident(uint32_t primitive, uint32_t node) const {
        return span(primitive,node).valid();
    }
    gfx::BufferHandle buffer() const { return buffer_; }
    bool valid() const { return device_ && device_->gpuHealthy() && buffer_.valid(); }
    const IndexResidencyStats& stats() const { return stats_; }
    void shutdown(gfx::Device& device);

private:
    const ModelAsset* asset_ = nullptr; // immutable borrowed CPU backing data
    gfx::Device* device_ = nullptr; // borrowed RHI, never cross-device destroy
    gfx::BufferHandle buffer_{};
    std::vector<std::vector<IndexSpan>> spans_;
    std::vector<std::vector<uint32_t>> parent_;
    // Implementation lives in StrataIndexPages.cpp. Only Metal/Vulkan with
    // checked storage uploads use stable index offsets. GLES3/GL33 retain
    // the Stage 15 immutable buffer-generation fallback.
    struct IncrementalStorage;
    std::unique_ptr<IncrementalStorage> incremental_;
    // Archive worker is never accessed after shutdown. Strata full CPU
    // assets remain immutable until disk metadata/page-table extraction
    // is decoupled from asset.indices in a subsequent stage.
    std::shared_ptr<DiskIndexPages> diskPages_;
    bool initializeIncremental(size_t uploadBudgetBytes, std::string* error);
    bool updateIncremental(const std::vector<std::vector<uint8_t>>& wanted,
                           const std::vector<IndexDemand>& priorities,
                           uint64_t deferred, std::string* error,
                           size_t uploadAllowanceBytes);
    void shutdownIncremental(gfx::Device& device);
    IndexResidencyStats stats_{};
};
} // namespace aiverse::render::strata
