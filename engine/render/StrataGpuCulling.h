#pragma once
// Experimental Metal/Vulkan STRATA GPU hierarchy selection.
// Metal produces diagnostic indexed ranges; Vulkan additionally produces
// guarded GPU-authored indirect argument candidates. Neither path bypasses
// Renderer3D's material, transform or exclusive draw ownership.

#include "StrataGpuScene.h"
#include "StrataView.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <string>
#include <vector>

namespace aiverse::render::strata {

// Object-to-world must include the Model node's bind transform. The caller
// supplies the same per-pass view/projection as used by CPU planView.
struct alignas(16) GpuSceneInstance {
    float objectToWorld[16] = {};
    uint32_t primitiveIndex = 0; // index within GpuMetadataHeader::primitiveCount
    uint32_t enabled = 1;
    uint32_t instanceToken = 0; // caller-owned stable diagnostic ID
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSceneInstance) == 80);
static_assert(alignof(GpuSceneInstance) == 16);

// The CPU oracle uses (pass viewProjection * this instance transform).
// Do not pass a precombined objectToClip matrix to the GPU dispatch.
GpuSceneInstance makeGpuSceneInstance(const mat4& objectToWorld,
                                     uint32_t primitiveIndex,
                                     uint32_t instanceToken,
                                     bool enabled = true);

struct GpuPassConfig {
    mat4 viewProjection{};
    int width = 0, height = 0;
    bool clipDepthZeroToOne = true;
    bool frustumCull = true;
    float errorThresholdPixels = 1.f;
    uint32_t maxOutputClusters = 65536;
    // Vulkan GPU indirect argument vertexOffset for *one compatible rigid
    // pooled source*. Materials/transforms and presentation remain owned by
    // Renderer3D. Must be nonnegative (source model gpuBaseVertex).
    int32_t baseVertex = 0;
    // Optional Vulkan-only *same-material, same-world* full-primitive
    // fallback command written by the second compute pass when GPU traversal
    // flags overflow/invalid metadata. RHI index buffer must contain the
    // original primitive and Strata pages in the SAME pooled storage.
    // Do not enable before the strict renderer-side draw contract passes.
    bool gpuFallbackEnabled = false;
    uint32_t gpuFallbackFirstIndex = 0, gpuFallbackIndexCount = 0;
    int32_t gpuFallbackBaseVertex = 0;
    // Stage17.4 GPU-only temporal LOD history. All fields below must be
    // provided together when hysteresisFraction>0. Zero (default) leaves
    // Metal/Vulkan GPU selection stateless and backward-compatible.
    float hysteresisFraction = 0.f; // 0..0.45; matches CPU StrataView
    uint32_t temporalPassId = 0; // main=1, sun=2, spot=3, reflection=4
    uint32_t temporalNodeCount = 0; // GLOBAL GPU metadata node count
    uint64_t temporalRevision = 0; // source revision + policy epoch
};
ViewConfig cpuReferenceView(const GpuPassConfig& pass,
                            const GpuSceneInstance& instance);

// Output is in arbitrary GPU atomic order. Each range is an absolute pooled
// index span (metadata v2 primitive base + node local firstIndex) and never
// carries material/transform state sufficient to call drawIndexed by itself.
struct alignas(16) GpuSelectedCluster {
    uint32_t instanceToken = 0;
    uint32_t gpuFirstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialGroup = 0;
    uint32_t nodeIndex = 0;
    uint32_t primitiveIndex = 0;
    uint32_t localFirstIndex = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSelectedCluster) == 32);

struct alignas(16) GpuSelectionCounters {
    uint32_t produced = 0; // may exceed output capacity when overflow occurs
    uint32_t overflow = 0; // output incomplete: NEVER submit these results
    uint32_t examined = 0; // diagnostic GPU node visits
    uint32_t invalid = 0;  // unexpected metadata / bounded stack / pass input
};
static_assert(sizeof(GpuSelectionCounters) == 16);

struct alignas(16) GpuSelectionUniforms {
    float viewProjection[16] = {};
    uint32_t instanceCount = 0;
    uint32_t outputCapacity = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float thresholdPixels = 1.f;
    uint32_t clipDepthZeroToOne = 1;
    uint32_t frustumCull = 1;
    uint32_t reserved = 0;
    uint32_t temporalNodeCount = 0;
    uint32_t temporalHasPrevious = 0;
    float hysteresisFraction = 0.f;
    uint32_t temporalReserved = 0;
};
static_assert(sizeof(GpuSelectionUniforms) == 112);

struct GpuSelectionSubmission {
    gfx::BufferHandle ranges;
    gfx::BufferHandle counters;
    // Stage 16.2 diagnostic GPU-authored VkDrawIndexedIndirectCommand array.
    // Only allocated by Vulkan. It is NOT automatically safe to render:
    // per-instance material/transform ownership must first be validated.
    gfx::BufferHandle indirect;
    uint32_t capacity = 0;
    uint32_t submittedInstances = 0;
    uint64_t frameSerial = 0;
    size_t submission = 0;
    bool temporalApplied = false;
    // GPU-written temporal mask for this submission; diagnostics only.
    // Only readable asynchronously AFTER a completed GPU frame.
    gfx::BufferHandle temporalState;
    bool valid() const {
        return ranges.valid() && counters.valid() && capacity != 0 &&
               frameSerial != 0;
    }
    bool gpuIndirectPrepared() const {
        return valid() && indirect.valid();
    }
};

// Readback is explicitly diagnostic and never drives production draws.
// Metal's asynchronous RHI readback is limited to 65536 bytes per request:
// at most 2048 selected-cluster records may be inspected per submission.
struct GpuSelectionReadback {
    enum class Status : uint8_t {
        Valid, Overflow, Invalid, ReadbackFailed
    };
    Status status = Status::ReadbackFailed;
    GpuSelectionCounters counters{};
    std::vector<GpuSelectedCluster> ranges;
    bool valid() const { return status == Status::Valid; }
};
using GpuSelectionReadbackFn = std::function<void(GpuSelectionReadback)>;

// Explicit CPU oracle comparison after a successful asynchronous readback.
// The pooledFirstIndex must belong to the same source + model revision as
// the metadata v2 dispatch. Rejects overflow/invalid results, mismatched
// per-instance cluster LOD and malformed source/instance information.
// GPU atomic compaction order does not matter; records are sorted by key.
bool compareGpuSelection(const ModelAsset& source,
                         const std::vector<GpuSceneInstance>& instances,
                         const GpuPassConfig& pass,
                         uint32_t pooledFirstIndex,
                         const GpuSelectionReadback& gpu,
                         std::string* error = nullptr,
                         // Diagnostic-only optional global metadata-node mask
                         // from the PREVIOUS accepted GPU/CPU frame. Null
                         // means cold start; GPU production never reads CPU.
                         const std::vector<uint8_t>* previousRefined = nullptr);

struct GpuTemporalStats {
    size_t retainedBytes=0, histories=0;
    uint64_t submitted=0, coldStarts=0, expired=0, refused=0;
    // Stage18 cumulative instrumentation. These are CPU-side RHI accounting
    // metrics, NOT measured GPU memory allocations or GPU execution time.
    size_t peakRetainedBytes=0, peakHistories=0;
    uint64_t successfulDispatches=0;
    size_t peakSubmissionsPerFrameSlot=0;
    // Stage18.3: backend-neutral CPU submission instrumentation.
    // Includes partial/failed submissions. These are NOT GPU timestamps.
    uint64_t computeBeginAttempts=0, computeEncodersOpened=0;
    uint64_t computeBeginFailures=0, computeDispatchAttempts=0;
    uint64_t computeDispatchAccepted=0, computeDispatchFailures=0;
    uint64_t computeGroupsRequested=0;
    uint64_t temporalResetDispatches=0, selectionDispatches=0;
    uint64_t indirectClearDispatches=0, safetyDispatches=0;
};

// One diagnostic selection dispatch per source and view; caller must retain
// the source's staged metadata+pooled index leases until GPU completion.
// No indirect command buffer is produced or consumed here.
class GpuClusterCulling {
public:
    GpuClusterCulling() = default;
    GpuClusterCulling(const GpuClusterCulling&) = delete;
    GpuClusterCulling& operator=(const GpuClusterCulling&) = delete;

    bool dispatch(gfx::Device& dev, gfx::BufferHandle gpuMetadataV2,
                  const std::vector<GpuSceneInstance>& instances,
                  const GpuPassConfig& config, uint64_t frameSerial,
                  GpuSelectionSubmission& out);
    // Must be called during the same GPU frame, AFTER dispatch, OUTSIDE
    // encoders. Callback arrives at a later Device::beginFrame on RHI thread.
    // A failed scheduling call reports failure via the callback once.
    bool requestReadback(gfx::Device& dev,
                         const GpuSelectionSubmission& submission,
                         GpuSelectionReadbackFn callback);
    void shutdown(gfx::Device& dev);
    GpuTemporalStats temporalStats() const { return temporalStats_; }
private:
    struct TemporalKey {
        uint32_t metadata=0, instanceToken=0, primitive=0, pass=0;
        uint64_t revision=0;
        bool operator<(const TemporalKey& b) const {
            return std::tie(metadata,instanceToken,primitive,pass,revision) <
                   std::tie(b.metadata,b.instanceToken,b.primitive,b.pass,b.revision);
        }
    };
    struct TemporalRecord {
        std::array<gfx::BufferHandle,3> state{};
        std::array<uint64_t,3> frame{};
        uint64_t lastFrame=0, lastAttemptFrame=0;
        uint32_t nodes=0,width=0,height=0;
        bool clipDepthZeroToOne=true;
        float fraction=0;
    };
    static constexpr size_t kMaxTemporalBytes=32u*1024u*1024u;
    static constexpr size_t kMaxTemporalHistories=256;
    std::map<TemporalKey,TemporalRecord> temporal_;
    GpuTemporalStats temporalStats_{};
    gfx::ComputeHandle temporalResetPipeline_{};
    gfx::ComputeHandle temporalSafetyPipeline_{};
    // GPU temporal state is a per-source-instance-pass mask, NEVER
    // a property of shared ModelAsset or a synchronous CPU readback.
    bool prepareTemporal(gfx::Device& dev,gfx::BufferHandle metadata,
                         const GpuSceneInstance& instance,
                         const GpuPassConfig& config,uint64_t frame,
                         gfx::BufferHandle& previous,
                         gfx::BufferHandle& next,bool& warm,
                         TemporalKey& key);
    void expireTemporal(gfx::Device& dev,uint64_t frame);
    void releaseTemporal(gfx::Device& dev);

    struct Buffers {
        gfx::BufferHandle instances, ranges, counters, indirect;
        size_t instancesBytes = 0, rangesBytes = 0, indirectBytes = 0;
    };
    static constexpr size_t kFramesInFlight = 3;
    static constexpr size_t kMaxSubmissionsPerFrame = 64;
    std::array<std::vector<Buffers>, kFramesInFlight> slots_{};
    std::array<uint64_t, kFramesInFlight> epochs_{};
    std::array<size_t, kFramesInFlight> cursors_{};
    gfx::ComputeHandle pipeline_{};
    gfx::ComputeHandle indirectClearPipeline_{};
    gfx::ComputeHandle indirectSafetyPipeline_{};
    static bool allocate(gfx::Device& dev, gfx::BufferHandle& handle,
                         size_t& capacity, size_t requested, const char* label);
};
} // namespace aiverse::render::strata
