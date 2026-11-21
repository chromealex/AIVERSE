#include "StrataGpuCulling.h"
#include "StrataVulkanCullingShaders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace aiverse::render::strata {
namespace {
// GPU selection is diagnostic until it is proven equivalent to CPU planView.
// Metal-only first path: other RHI implementations must return false safely.
constexpr const char* kMetalCulling = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Header {
    uint version, primitiveCount, nodeCount, rootCount;
    uint primitivesOffset, nodesOffset, rootsOffset, reserved;
};
struct Primitive {
    uint mesh, primitive, materialGroup, nodeBase;
    uint nodeCount, rootBase, rootCount, gpuFirstIndex;
};
struct Node {
    float4 lo, hi;
    uint firstChild, childCount, sourceFirstIndex, sourceIndexCount;
    uint materialGroup, resident;
    float geometricError;
    uint reserved;
};
struct Instance {
    float4 c0, c1, c2, c3;
    uint primitiveIndex, enabled, instanceToken, reserved;
};
struct Selected {
    uint instanceToken, gpuFirstIndex, indexCount, materialGroup;
    uint nodeIndex, primitiveIndex, localFirstIndex, reserved;
};
struct Params {
    float4x4 viewProjection;
    uint instanceCount, outputCapacity, width, height;
    float thresholdPixels;
    uint clipDepthZeroToOne, frustumCull, reserved;
    uint temporalNodeCount, temporalHasPrevious;
    float hysteresisFraction;
    uint temporalReserved;
};

inline float projectError(float4x4 clip, Node node, Params params) {
    float nearW = 1.0e30f, maxX = 0.0f, maxY = 0.0f;
    for (uint c = 0; c < 8; ++c) {
        float3 p = float3((c & 1) ? node.hi.x : node.lo.x,
                          (c & 2) ? node.hi.y : node.lo.y,
                          (c & 4) ? node.hi.z : node.lo.z);
        float4 q = clip * float4(p, 1.0f);
        if (!all(isfinite(q)) || q.w <= 0.0f ||
            q.z < (params.clipDepthZeroToOne ? 0.0f : -q.w))
            return 1.0e30f; // conservative near/behind camera
        nearW = min(nearW, q.w);
        maxX = max(maxX, abs(q.x));
        maxY = max(maxY, abs(q.y));
    }
    float error = node.geometricError;
    float nx = length(float3(clip[0][0], clip[1][0], clip[2][0]));
    float ny = length(float3(clip[0][1], clip[1][1], clip[2][1]));
    float nw = length(float3(clip[0][3], clip[1][3], clip[2][3]));
    float delta = nw * error;
    if (nearW <= delta) return 1.0e30f;
    float pixels = 0.5f * max(
        float(params.width) * error * (nx * nearW + maxX * nw),
        float(params.height) * error * (ny * nearW + maxY * nw)) /
        (nearW * (nearW - delta));
    return isfinite(pixels) ? pixels : 1.0e30f;
}
inline bool outsideFrustum(float4x4 clip, Node n) {
    uint allOutside = 63u;
    for (uint c = 0; c < 8; ++c) {
        float3 p = float3((c & 1) ? n.hi.x : n.lo.x,
                          (c & 2) ? n.hi.y : n.lo.y,
                          (c & 4) ? n.hi.z : n.lo.z);
        float4 q = clip * float4(p, 1.0f);
        if (!all(isfinite(q))) return false;
        uint outside = 0;
        if (q.x < -q.w) outside |= 1u;
        if (q.x >  q.w) outside |= 2u;
        if (q.y < -q.w) outside |= 4u;
        if (q.y >  q.w) outside |= 8u;
        if (q.z < -q.w) outside |= 16u; // match current geometryBoxVisible oracle
        if (q.z >  q.w) outside |= 32u;
        allOutside &= outside;
    }
    return allOutside != 0;
}
inline void markInvalid(device atomic_uint* counters) {
    atomic_fetch_or_explicit(counters + 3, 1u, memory_order_relaxed);
}
kernel void cs_main(device const uchar* metadata [[buffer(0)]],
                    device const Instance* instances [[buffer(1)]],
                    device Selected* output [[buffer(2)]],
                    device atomic_uint* counters [[buffer(3)]],
                    device const uint* previousRefined [[buffer(5)]],
                    device uint* nextRefined [[buffer(6)]],
                    constant Params& params [[buffer(8)]],
                    uint tid [[thread_position_in_grid]]) {
    if (tid >= params.instanceCount) return;
    device const Header* header = (device const Header*)metadata;
    if (header->version != 2u || header->primitiveCount == 0 ||
        header->nodeCount == 0 || header->rootCount == 0) {
        markInvalid(counters);
        return;
    }
    const Instance inst = instances[tid];
    if (!inst.enabled) return;
    if (inst.primitiveIndex >= header->primitiveCount) {
        markInvalid(counters);
        return;
    }
    device const Primitive* primitives =
        (device const Primitive*)(metadata + header->primitivesOffset);
    device const Node* nodes =
        (device const Node*)(metadata + header->nodesOffset);
    device const uint* roots =
        (device const uint*)(metadata + header->rootsOffset);
    const Primitive primitive = primitives[inst.primitiveIndex];
    if (primitive.gpuFirstIndex == 0xffffffffu ||
        primitive.rootBase > header->rootCount ||
        primitive.rootCount > header->rootCount - primitive.rootBase ||
        primitive.nodeBase > header->nodeCount ||
        primitive.nodeCount > header->nodeCount - primitive.nodeBase) {
        markInvalid(counters);
        return;
    }
    const float4x4 objectToClip = params.viewProjection *
        float4x4(inst.c0, inst.c1, inst.c2, inst.c3);

    // One thread traverses one material primitive instance. No shared queue
    // or unbounded recursion. Parent fallback occurs when ANY child is absent.
    for (uint ri = 0; ri < primitive.rootCount; ++ri) {
        uint stack[128];
        uint top = 0;
        const uint root = roots[primitive.rootBase + ri];
        if (root < primitive.nodeBase ||
            root >= primitive.nodeBase + primitive.nodeCount) {
            markInvalid(counters);
            return;
        }
        stack[top++] = root;
        uint visited = 0;
        while (top) {
            if (++visited > primitive.nodeCount) {
                markInvalid(counters);
                return; // corrupt graph / unexpected cycle
            }
            const uint id = stack[--top];
            if (id >= header->nodeCount) {
                markInvalid(counters);
                return;
            }
            const Node node = nodes[id];
            atomic_fetch_add_explicit(counters + 2, 1u, memory_order_relaxed);
            if (!node.resident) {
                markInvalid(counters); // parent/root residency promise broken
                return;
            }
            float threshold=params.thresholdPixels;
            if(params.hysteresisFraction>0.0f &&
               params.temporalHasPrevious!=0u &&
               id<params.temporalNodeCount){
                const bool wasFine=previousRefined[id]!=0u;
                threshold*=wasFine
                    ? (1.0f-params.hysteresisFraction)
                    : (1.0f+params.hysteresisFraction);
            }
            bool refine = node.childCount != 0 &&
                projectError(objectToClip, node, params) > threshold;
            if (refine) {
                if (node.firstChild < primitive.nodeBase ||
                    node.firstChild >= primitive.nodeBase + primitive.nodeCount ||
                    node.childCount > primitive.nodeBase +
                        primitive.nodeCount - node.firstChild) {
                    markInvalid(counters);
                    return;
                }
                for (uint c = 0; c < node.childCount; ++c)
                    if (!nodes[node.firstChild + c].resident) {
                        refine = false;
                        break;
                    }
                if (refine && top + node.childCount > 128u) {
                    markInvalid(counters); // NO truncated output may be rendered
                    return;
                }
            }
            if(params.hysteresisFraction>0.0f &&
               id<params.temporalNodeCount)
                nextRefined[id]=refine?1u:0u;
            if (refine) {
                for (uint c = node.childCount; c > 0; --c)
                    stack[top++] = node.firstChild + c - 1u;
                continue;
            }
            if (params.frustumCull &&
                outsideFrustum(objectToClip, node))
                continue;
            if (!node.sourceIndexCount ||
                primitive.gpuFirstIndex >
                    0xffffffffu - node.sourceFirstIndex) {
                markInvalid(counters);
                return;
            }
            uint at = atomic_fetch_add_explicit(
                counters, 1u, memory_order_relaxed);
            if (at >= params.outputCapacity) {
                atomic_fetch_or_explicit(counters + 1, 1u,
                                         memory_order_relaxed);
                continue;
            }
            Selected selected;
            selected.instanceToken = inst.instanceToken;
            selected.gpuFirstIndex =
                primitive.gpuFirstIndex + node.sourceFirstIndex;
            selected.indexCount = node.sourceIndexCount;
            selected.materialGroup = node.materialGroup;
            selected.nodeIndex = id;
            selected.primitiveIndex = inst.primitiveIndex;
            selected.localFirstIndex = node.sourceFirstIndex;
            selected.reserved = 0;
            output[at] = selected;
        }
    }
}
)MSL";
// The GPU resets ALL refined bits for the current frame; only traversed
// parent nodes are set. This is exactly CPU SelectionOutput::refinedNodes.
constexpr const char* kMetalTemporalReset=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Params { uint count; };
kernel void cs_main(device uint* state [[buffer(0)]],
                    constant Params& p [[buffer(8)]],
                    uint id [[thread_position_in_grid]]) {
    if(id<p.count)state[id]=0u;
}
)MSL";

// Restore the entire previous GPU history after invalid/capacity-overflow
// selection. Unlike CPU readback this happens in the same GPU command queue
// and never changes Renderer3D's chosen draw owner.
constexpr const char* kMetalTemporalSafety=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Params { uint count, hasPrevious; };
kernel void cs_main(device const atomic_uint* counters [[buffer(0)]],
                    device const uint* previous [[buffer(1)]],
                    device uint* next [[buffer(2)]],
                    constant Params& p [[buffer(8)]],
                    uint id [[thread_position_in_grid]]) {
    if(id>=p.count)return;
    const uint overflow=atomic_load_explicit(counters+1,memory_order_relaxed);
    const uint invalid=atomic_load_explicit(counters+3,memory_order_relaxed);
    if(overflow!=0u || invalid!=0u)
        next[id]=p.hasPrevious!=0u?previous[id]:0u;
}
)MSL";
} // namespace

GpuSceneInstance makeGpuSceneInstance(const mat4& objectToWorld,
                                     uint32_t primitiveIndex,
                                     uint32_t instanceToken, bool enabled) {
    GpuSceneInstance instance{};
    std::memcpy(instance.objectToWorld, objectToWorld.m,
                sizeof(instance.objectToWorld));
    instance.primitiveIndex = primitiveIndex;
    instance.instanceToken = instanceToken;
    instance.enabled = enabled ? 1u : 0u;
    return instance;
}

ViewConfig cpuReferenceView(const GpuPassConfig& pass,
                            const GpuSceneInstance& instance) {
    mat4 world{};
    std::memcpy(world.m, instance.objectToWorld, sizeof(world.m));
    ViewConfig reference{};
    reference.objectToClip = pass.viewProjection * world;
    reference.width = pass.width;
    reference.height = pass.height;
    reference.clipDepthZeroToOne = pass.clipDepthZeroToOne;
    reference.frustumCull = pass.frustumCull;
    reference.errorThresholdPixels = pass.errorThresholdPixels;
    reference.maxSelectedNodes = pass.maxOutputClusters;
    reference.hysteresisFraction=pass.hysteresisFraction;
    return reference;
}

bool compareGpuSelection(const ModelAsset& source,
                         const std::vector<GpuSceneInstance>& instances,
                         const GpuPassConfig& pass,
                         uint32_t pooledFirstIndex,
                         const GpuSelectionReadback& gpu,
                         std::string* error,
                         const std::vector<uint8_t>* previousRefined) {
    if (error) error->clear();
    auto fail = [&](const std::string& reason) {
        if (error) *error = reason;
        return false;
    };
    if (!gpu.valid() || gpu.counters.overflow || gpu.counters.invalid ||
        gpu.counters.produced != gpu.ranges.size())
        return fail("GPU range readback was incomplete, invalid or overflowed");
    if (source.primitives.empty() || instances.empty())
        return fail("empty Strata source or instance set");
    using Key = std::array<uint32_t, 7>;
    std::vector<Key> expected;
    std::vector<Key> actual;
    std::set<uint32_t> tokens;

    for (const auto& item : gpu.ranges) {
        actual.push_back({item.instanceToken, item.gpuFirstIndex,
                          item.indexCount, item.materialGroup,
                          item.nodeIndex, item.primitiveIndex,
                          item.localFirstIndex});
    }
    if(!std::isfinite(pass.hysteresisFraction) ||
       pass.hysteresisFraction<0.f || pass.hysteresisFraction>.45f)
        return fail("GPU temporal policy invalid for CPU reference");
    std::vector<uint64_t> primitiveIndexOffsets(source.primitives.size());
    std::vector<uint64_t> primitiveNodeOffsets(source.primitives.size());
    uint64_t indexCursor = 0, nodeCursor = 0;
    for (size_t i = 0; i < source.primitives.size(); ++i) {
        primitiveIndexOffsets[i] = indexCursor;
        primitiveNodeOffsets[i] = nodeCursor;
        indexCursor += source.primitives[i].geometry.indices.size();
        nodeCursor += source.primitives[i].geometry.nodes.size();
        if (indexCursor + uint64_t(pooledFirstIndex) >
                std::numeric_limits<uint32_t>::max() ||
            nodeCursor > std::numeric_limits<uint32_t>::max())
            return fail("Strata source exceeds GPU index/node address range");
    }
    if(previousRefined && previousRefined->size()!=nodeCursor)
        return fail("previous GPU temporal mask does not match source metadata nodes");
    for (const auto& instance : instances) {
        if (!instance.enabled) continue;
        if (!instance.instanceToken ||
            !tokens.insert(instance.instanceToken).second ||
            instance.primitiveIndex >= source.primitives.size())
            return fail("GPU instance token or primitive reference invalid");
        const auto& p = source.primitives[instance.primitiveIndex];
        ViewPlan cpu;
        std::string reason;
        ViewConfig cpuView=cpuReferenceView(pass,instance);
        std::vector<uint8_t> localHistory;
        if(previousRefined){
            const size_t begin=size_t(primitiveNodeOffsets[instance.primitiveIndex]);
            localHistory.assign(previousRefined->begin()+begin,
                previousRefined->begin()+begin+p.geometry.nodes.size());
            cpuView.previousRefined=&localHistory;
        }
        if (!planView(p.geometry,cpuView,cpu,&reason))
            return fail("CPU reference selection failed: " + reason);
        // CPU can decide to retain a resident parent under budget pressure.
        // GPU emits overflow instead, so those cases are not equivalent.
        if (cpu.budgetExceeded)
            return fail("CPU reference used a budget fallback; not GPU-parity comparable");
        for (const auto& range : cpu.ranges) {
            const uint64_t offset = uint64_t(pooledFirstIndex) +
                primitiveIndexOffsets[instance.primitiveIndex] + range.firstIndex;
            const uint64_t node = primitiveNodeOffsets[instance.primitiveIndex] +
                range.node;
            if (offset > std::numeric_limits<uint32_t>::max() ||
                node > std::numeric_limits<uint32_t>::max())
                return fail("CPU reference output address overflow");
            expected.push_back({
                instance.instanceToken, uint32_t(offset), range.indexCount,
                range.materialGroup, uint32_t(node), instance.primitiveIndex,
                range.firstIndex
            });
        }
    }
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    if (expected != actual)
        return fail("Strata GPU/CPU visible cluster mismatch: expected " +
                    std::to_string(expected.size()) + " vs GPU " +
                    std::to_string(actual.size()) + " ranges");
    return true;
}

bool GpuClusterCulling::requestReadback(
    gfx::Device& dev, const GpuSelectionSubmission& submission,
    GpuSelectionReadbackFn callback) {
    if (!callback) return false;
    const auto reject = [&]() {
        GpuSelectionReadback failed;
        callback(std::move(failed));
        return false;
    };
    if ((dev.info().backend != gfx::Backend::Metal &&
         dev.info().backend != gfx::Backend::Vulkan) ||
        !submission.valid() || submission.capacity > 2048 ||
        submission.frameSerial != dev.frameSerial())
        return reject();
    const size_t frameSlot = submission.frameSerial % kFramesInFlight;
    if (epochs_[frameSlot] != submission.frameSerial ||
        submission.submission >= cursors_[frameSlot] ||
        submission.submission >= slots_[frameSlot].size())
        return reject();
    const auto& slot = slots_[frameSlot][submission.submission];
    if (submission.ranges != slot.ranges ||
        submission.counters != slot.counters)
        return reject();

    struct Pending {
        GpuSelectionReadbackFn callback;
        std::vector<uint8_t> counters;
        std::vector<uint8_t> ranges;
        bool countersReady = false, rangesReady = false, delivered = false;
    };
    auto pending = std::make_shared<Pending>();
    pending->callback = std::move(callback);
    const auto maybeComplete = [](const std::shared_ptr<Pending>& state) {
        if (!state->countersReady || !state->rangesReady ||
            state->delivered) return;
        state->delivered = true;
        GpuSelectionReadback result;
        if (state->counters.size() == sizeof(GpuSelectionCounters)) {
            std::memcpy(&result.counters, state->counters.data(),
                        sizeof(result.counters));
            const size_t produced = result.counters.produced;
            if (result.counters.invalid) {
                result.status = GpuSelectionReadback::Status::Invalid;
            } else if (result.counters.overflow ||
                       produced > state->ranges.size() /
                           sizeof(GpuSelectedCluster)) {
                result.status = GpuSelectionReadback::Status::Overflow;
            } else if (state->ranges.size() % sizeof(GpuSelectedCluster) == 0) {
                result.status = GpuSelectionReadback::Status::Valid;
                result.ranges.resize(produced);
                if (produced)
                    std::memcpy(result.ranges.data(), state->ranges.data(),
                                produced * sizeof(GpuSelectedCluster));
            }
        }
        state->callback(std::move(result));
    };
    const bool first = dev.readBufferAsync(
        slot.counters, sizeof(GpuSelectionCounters),
        [pending, maybeComplete](std::vector<uint8_t> bytes) {
            pending->counters = std::move(bytes);
            pending->countersReady = true;
            maybeComplete(pending);
        });
    const bool second = first && dev.readBufferAsync(
        slot.ranges, size_t(submission.capacity) * sizeof(GpuSelectedCluster),
        [pending, maybeComplete](std::vector<uint8_t> bytes) {
            pending->ranges = std::move(bytes);
            pending->rangesReady = true;
            maybeComplete(pending);
        });
    if (!second) {
        pending->countersReady = pending->rangesReady = true;
        pending->counters.clear();
        pending->ranges.clear();
        maybeComplete(pending);
        return false;
    }
    return true;
}

bool GpuClusterCulling::allocate(gfx::Device& dev, gfx::BufferHandle& h,
                                 size_t& bytes, size_t needed,
                                 const char* label) {
    if (h.valid() && bytes >= needed) return true;
    gfx::BufferDesc desc{};
    desc.type = gfx::BufferType::Storage;
    desc.usage = gfx::Usage::Immutable;
    desc.size = needed;
    desc.label = label;
    const gfx::BufferHandle created = dev.createBuffer(desc);
    if (!created.valid()) return false;
    if (h.valid()) dev.destroyBuffer(h);
    h = created;
    bytes = needed;
    return true;
}

// One bounded history per metadata source, original primitive,
// stable instance token, camera/pass slot and source/policy revision.
// A GPU-written mask is never overwritten until its triple-buffer frame
// slot is confirmed complete; previous and next are separate resources.
void GpuClusterCulling::expireTemporal(gfx::Device& dev,uint64_t frame) {
    const uint64_t completed=dev.completedFrameSerial();
    if(!completed)return;
    for(auto it=temporal_.begin();it!=temporal_.end();){
        const auto& record=it->second;
        const uint64_t lastUsed=std::max(
            record.lastFrame,record.lastAttemptFrame);
        bool inFlight=false;
        for(const uint64_t epoch:record.frame)
            if(epoch && epoch>completed)inFlight=true;
        if(!lastUsed || frame<=lastUsed ||
           frame-lastUsed<=120 || inFlight) {
            ++it;
            continue;
        }
        for(const auto handle:record.state)
            if(handle.valid()){
                dev.destroyBuffer(handle);
                temporalStats_.retainedBytes-=size_t(record.nodes)*sizeof(uint32_t);
            }
        ++temporalStats_.expired;
        it=temporal_.erase(it);
    }
    temporalStats_.histories=temporal_.size();
}

void GpuClusterCulling::releaseTemporal(gfx::Device& dev) {
    for(auto& [key,record]:temporal_)
        for(const auto handle:record.state)
            if(handle.valid())dev.destroyBuffer(handle);
    temporal_.clear();
    if(temporalResetPipeline_.valid())
        dev.destroyCompute(temporalResetPipeline_);
    if(temporalSafetyPipeline_.valid())
        dev.destroyCompute(temporalSafetyPipeline_);
    temporalResetPipeline_={};
    temporalSafetyPipeline_={};
    temporalStats_={};
}

bool GpuClusterCulling::prepareTemporal(
        gfx::Device& dev,gfx::BufferHandle metadata,
        const GpuSceneInstance& instance,const GpuPassConfig& config,
        uint64_t frame,gfx::BufferHandle& previous,
        gfx::BufferHandle& next,bool& warm,TemporalKey& key) {
    if(!metadata.valid() || !instance.enabled || !instance.instanceToken ||
       !config.temporalPassId || !config.temporalRevision ||
       config.temporalNodeCount==0 || config.temporalNodeCount>262144 ||
       !std::isfinite(config.hysteresisFraction) ||
       config.hysteresisFraction<=0.f ||
       config.hysteresisFraction>.45f)return false;
    expireTemporal(dev,frame);
    key={metadata.id,instance.instanceToken,instance.primitiveIndex,
         config.temporalPassId,config.temporalRevision};
    auto it=temporal_.find(key);
    const bool newlyCreated=it==temporal_.end();
    if(newlyCreated){
        if(temporal_.size()>=kMaxTemporalHistories)return false;
        TemporalRecord fresh{};
        fresh.nodes=config.temporalNodeCount;
        it=temporal_.emplace(key,fresh).first;
    }
    TemporalRecord& record=it->second;
    if(record.nodes!=config.temporalNodeCount ||
       (record.lastFrame>=frame && record.lastFrame!=0))
        return false;
    const size_t slot=size_t(frame%kFramesInFlight);
    // No retry of an attempted key in the same GPU encoder epoch: even
    // failed later passes can leave GPU commands referencing this slot.
    if(record.frame[slot]==frame)return false;
    if(record.frame[slot] && record.frame[slot]!=frame &&
       (dev.completedFrameSerial()==0 ||
        record.frame[slot]>dev.completedFrameSerial()))
        return false;
    // If no submission occurred in the previous two frame epochs, old
    // history is considered cold. This also avoids using the output slot
    // as its own previous input after a triple-buffer wrap.
    warm=record.lastFrame && frame>record.lastFrame &&
         frame-record.lastFrame<kFramesInFlight &&
         record.width==uint32_t(config.width) &&
         record.height==uint32_t(config.height) &&
         record.clipDepthZeroToOne==config.clipDepthZeroToOne &&
         record.fraction==config.hysteresisFraction;
    previous={};
    if(warm){
        const size_t previousSlot=size_t(record.lastFrame%kFramesInFlight);
        if(previousSlot==slot ||
           !record.state[previousSlot].valid() ||
           record.frame[previousSlot]!=record.lastFrame)
            warm=false;
        else previous=record.state[previousSlot];
    }
    const size_t bytes=size_t(record.nodes)*sizeof(uint32_t);
    if(!record.state[slot].valid()){
        if(bytes>kMaxTemporalBytes-temporalStats_.retainedBytes){
            if(newlyCreated)temporal_.erase(it);
            return false;
        }
        std::vector<uint32_t> zeros(record.nodes,0);
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Storage;
        desc.usage=gfx::Usage::Immutable;
        desc.size=bytes;
        desc.data=zeros.data();
        desc.label="STRATA GPU temporal LOD refined mask";
        record.state[slot]=dev.createBuffer(desc);
        if(!record.state[slot].valid()){
            if(newlyCreated)temporal_.erase(it);
            return false;
        }
        temporalStats_.retainedBytes+=bytes;
        temporalStats_.peakRetainedBytes=std::max(
            temporalStats_.peakRetainedBytes,temporalStats_.retainedBytes);
    }
    next=record.state[slot];
    if(!warm)previous=next; // shader does not read previous on cold start
    // Mark attempted GPU use BEFORE encoding any compute commands. A later
    // failure cannot permit this buffer to be reused by a younger frame
    // whose RHI completion fence has not reached the attempted epoch.
    record.frame[slot]=frame;
    record.lastAttemptFrame=frame;
    temporalStats_.histories=temporal_.size();
    temporalStats_.peakHistories=std::max(
        temporalStats_.peakHistories,temporalStats_.histories);
    return true;
}

bool GpuClusterCulling::dispatch(gfx::Device& dev,
                                gfx::BufferHandle gpuMetadataV2,
                                const std::vector<GpuSceneInstance>& instances,
                                const GpuPassConfig& config, uint64_t frameSerial,
                                GpuSelectionSubmission& out) {
    const bool vulkan=dev.info().backend==gfx::Backend::Vulkan;
    const bool temporal=config.hysteresisFraction>0.f;
    if ((dev.info().backend != gfx::Backend::Metal && !vulkan) ||
        !std::isfinite(config.hysteresisFraction) ||
        config.hysteresisFraction<0.f || config.hysteresisFraction>.45f ||
        (temporal && (instances.size()!=1 ||
                      !config.temporalPassId ||
                      !config.temporalRevision ||
                      !config.temporalNodeCount ||
                      config.temporalNodeCount>262144 ||
                      !instances[0].enabled)) ||
        !dev.supportsCompute() || !gpuMetadataV2.valid() ||
        (vulkan && (config.maxOutputClusters>2048 ||
                    config.baseVertex<0 ||
                    (config.gpuFallbackEnabled &&
                     (!config.gpuFallbackIndexCount ||
                      config.gpuFallbackIndexCount%3u!=0 ||
                      config.gpuFallbackBaseVertex<0 ||
                      config.gpuFallbackBaseVertex!=config.baseVertex)))) ||
        !frameSerial || instances.empty() || instances.size() > 4096 ||
        config.maxOutputClusters == 0 || config.maxOutputClusters > 65536 ||
        config.width <= 0 || config.height <= 0 ||
        config.width > 32768 || config.height > 32768 ||
        !std::isfinite(config.errorThresholdPixels) ||
        config.errorThresholdPixels <= 0.f ||
        config.errorThresholdPixels > 10000.f)
        return false;
    for (float v : config.viewProjection.m)
        if (!std::isfinite(v)) return false;
    for (const auto& inst : instances) {
        for (float v : inst.objectToWorld)
            if (!std::isfinite(v)) return false;
    }
    // If the RHI exposes a serial, reject callers passing a different
    // epoch: otherwise a stale frame-slot could be overwritten in-flight.
    if (dev.frameSerial() != 0 && dev.frameSerial() != frameSerial)
        return false;
    if (!pipeline_.valid()) {
        pipeline_ = dev.createCompute({vulkan ? nullptr : kMetalCulling,
                                       vulkan ? kVulkanGpuCulling : nullptr,
                                       "Strata diagnostic GPU cluster culling",gfx::VulkanComputeLayout::BufferPushConstants});
        if (!pipeline_.valid()) return false;
    }
    if(temporal){
        if(!temporalResetPipeline_.valid())
            temporalResetPipeline_=dev.createCompute({
                vulkan?nullptr:kMetalTemporalReset,
                vulkan?kVulkanTemporalReset:nullptr,
                "STRATA GPU temporal refinement reset",gfx::VulkanComputeLayout::BufferPushConstants});
        if(!vulkan && !temporalSafetyPipeline_.valid())
            temporalSafetyPipeline_=dev.createCompute({
                kMetalTemporalSafety,nullptr,
                "STRATA GPU temporal invalid-selection rollback"});
        if(!temporalResetPipeline_.valid() ||
           (!vulkan && !temporalSafetyPipeline_.valid()))
            return false;
    }
    if(vulkan){
        if(!indirectClearPipeline_.valid())
            indirectClearPipeline_=dev.createCompute({nullptr,
                kVulkanIndirectClear,"Strata Vulkan indirect clear",gfx::VulkanComputeLayout::BufferPushConstants});
        if(!indirectSafetyPipeline_.valid())
            indirectSafetyPipeline_=dev.createCompute({nullptr,
                kVulkanIndirectSafety,"Strata Vulkan indirect safety",gfx::VulkanComputeLayout::BufferPushConstants});
        if(!indirectClearPipeline_.valid() ||
           !indirectSafetyPipeline_.valid())return false;
    }
    const size_t index = frameSerial % kFramesInFlight;
    if (epochs_[index] != frameSerial) {
        // Backend must guarantee a completed frame slot before reuse, like
        // the existing GpuSceneDrawCommands 3-frame-slot implementation.
        if (epochs_[index] != 0 &&
            (dev.completedFrameSerial() == 0 ||
             dev.completedFrameSerial() < epochs_[index]))
            return false;
        epochs_[index] = frameSerial;
        cursors_[index] = 0;
    }
    const size_t submission = cursors_[index];
    if (submission >= kMaxSubmissionsPerFrame) return false;
    auto& pool = slots_[index];
    if (submission == pool.size()) pool.emplace_back();
    auto& slot = pool[submission];
    const size_t inputBytes = instances.size() * sizeof(GpuSceneInstance);
    const size_t outputBytes =
        size_t(config.maxOutputClusters) * sizeof(GpuSelectedCluster);
    if (!allocate(dev, slot.instances, slot.instancesBytes, inputBytes,
                  "Strata diagnostic GPU instances") ||
        !allocate(dev, slot.ranges, slot.rangesBytes, outputBytes,
                  "Strata diagnostic visible cluster ranges")) return false;
    if(vulkan && (!slot.indirect.valid() ||
                  slot.indirectBytes < size_t(config.maxOutputClusters) *
                      sizeof(gfx::DrawIndexedIndirectCommand))){
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Indirect;
        desc.usage=gfx::Usage::Immutable;
        desc.size=size_t(config.maxOutputClusters) *
                  sizeof(gfx::DrawIndexedIndirectCommand);
        desc.label="Strata Vulkan GPU-authored indexed draws";
        const auto created=dev.createBuffer(desc);
        if(!created.valid())return false;
        if(slot.indirect.valid())dev.destroyBuffer(slot.indirect);
        slot.indirect=created;
        slot.indirectBytes=desc.size;
    }
    if (!slot.counters.valid()) {
        gfx::BufferDesc desc{};
        desc.type = gfx::BufferType::Storage;
        desc.usage = gfx::Usage::Immutable;
        desc.size = sizeof(GpuSelectionCounters);
        desc.label = "Strata diagnostic GPU counters";
        slot.counters = dev.createBuffer(desc);
        if (!slot.counters.valid()) return false;
    }
    gfx::BufferHandle previousTemporal{},nextTemporal{};
    TemporalKey temporalKey{};
    bool warmTemporal=false;
    if(temporal && !prepareTemporal(
        dev,gpuMetadataV2,instances.front(),config,frameSerial,
        previousTemporal,nextTemporal,warmTemporal,temporalKey)){
        ++temporalStats_.refused;
        return false; // caller uses unchanged CPU/Legacy draw route
    }
    // Stage18.3 records RHI submission overhead without GPU readbacks,
    // CPU clocks or changes to queue ordering. Count partial attempts too.
    const auto trackedBegin=[&](gfx::ComputeHandle pipeline) {
        ++temporalStats_.computeBeginAttempts;
        if(!dev.beginCompute(pipeline)){
            ++temporalStats_.computeBeginFailures;
            return false;
        }
        ++temporalStats_.computeEncodersOpened;
        return true;
    };
    const auto trackedDispatch=[&](uint32_t groups,uint64_t& stageCount) {
        ++temporalStats_.computeDispatchAttempts;
        temporalStats_.computeGroupsRequested+=groups;
        ++stageCount;
        const bool ok=dev.dispatchChecked(groups);
        if(ok)++temporalStats_.computeDispatchAccepted;
        else ++temporalStats_.computeDispatchFailures;
        return ok;
    };
    const GpuSelectionCounters zero{};
    dev.updateBuffer(slot.instances, instances.data(), inputBytes);
    // GPU-only temporal mask reset; the previous frame's buffer is
    // strictly read-only and kept alive by the bounded triple ring.
    if(temporal){
        if(!trackedBegin(temporalResetPipeline_))return false;
        dev.setComputeBuffer(0,nextTemporal);
        const uint32_t count=config.temporalNodeCount;
        dev.setComputeUniforms(&count,sizeof(count));
        const bool cleared=trackedDispatch(count,temporalStats_.temporalResetDispatches);
        dev.endCompute();
        if(!cleared)return false;
    }
    dev.updateBuffer(slot.counters, &zero, sizeof(zero));
    if(vulkan){
        // First GPU pass clears all packet slots. It never requires
        // a CPU readback or uploads a full zero-argument array.
        if(!trackedBegin(indirectClearPipeline_))return false;
        dev.setComputeBuffer(0,slot.indirect);
        const uint32_t capacity=config.maxOutputClusters;
        dev.setComputeUniforms(&capacity,sizeof(capacity));
        const bool cleared=trackedDispatch(capacity,temporalStats_.indirectClearDispatches);
        dev.endCompute();
        if(!cleared)return false;
    }
    if (!trackedBegin(pipeline_)) return false;
    dev.setComputeBuffer(0, gpuMetadataV2);
    dev.setComputeBuffer(1, slot.instances);
    dev.setComputeBuffer(2, slot.ranges);
    dev.setComputeBuffer(3, slot.counters);
    if(vulkan)dev.setComputeBuffer(4,slot.indirect);
    if(temporal){
        dev.setComputeBuffer(5,previousTemporal);
        dev.setComputeBuffer(6,nextTemporal);
    }else{
        // Keep GPU shader resource slots valid on BOTH Vulkan and Metal
        // even while the optional temporal branch is disabled. The kernel
        // never touches these counters at hysteresisFraction=0.
        dev.setComputeBuffer(5,slot.counters);
        dev.setComputeBuffer(6,slot.counters);
    }
    GpuSelectionUniforms uniforms{};
    std::memcpy(uniforms.viewProjection, config.viewProjection.m,
                sizeof(uniforms.viewProjection));
    uniforms.instanceCount = uint32_t(instances.size());
    uniforms.outputCapacity = config.maxOutputClusters;
    uniforms.width = uint32_t(config.width);
    uniforms.height = uint32_t(config.height);
    uniforms.thresholdPixels = config.errorThresholdPixels;
    uniforms.clipDepthZeroToOne = config.clipDepthZeroToOne ? 1u : 0u;
    uniforms.frustumCull = config.frustumCull ? 1u : 0u;
    uniforms.reserved=uint32_t(config.baseVertex);
    uniforms.temporalNodeCount=temporal?config.temporalNodeCount:0;
    uniforms.temporalHasPrevious=warmTemporal?1u:0u;
    uniforms.hysteresisFraction=temporal?config.hysteresisFraction:0.f;
    dev.setComputeUniforms(&uniforms, sizeof(uniforms));
    const bool encoded=trackedDispatch(uint32_t(instances.size()),temporalStats_.selectionDispatches);
    dev.endCompute();
    if(!encoded) return false;
    if(temporal && !vulkan){
        if(!trackedBegin(temporalSafetyPipeline_))return false;
        dev.setComputeBuffer(0,slot.counters);
        dev.setComputeBuffer(1,previousTemporal);
        dev.setComputeBuffer(2,nextTemporal);
        const uint32_t control[2]{
            config.temporalNodeCount,warmTemporal?1u:0u};
        dev.setComputeUniforms(control,sizeof(control));
        const bool safe=trackedDispatch(config.temporalNodeCount,temporalStats_.safetyDispatches);
        dev.endCompute();
        if(!safe)return false;
    }
    if(vulkan){
        // Second GPU-only validation consumes counters from the selection
        // pass; invalid metadata/stack traversal or overflow zeroes ALL
        // GPU indirect indexCount fields. An incomplete packet can never
        // silently become partially visible geometry.
        if(!trackedBegin(indirectSafetyPipeline_))return false;
        dev.setComputeBuffer(0,slot.indirect);
        dev.setComputeBuffer(1,slot.counters);
        struct SafetyUniforms {
            uint32_t capacity=0, enableFallback=0, firstIndex=0, indexCount=0;
            int32_t baseVertex=0;
            uint32_t temporalNodeCount=0, temporalHasPrevious=0;
        } safety{};
        static_assert(sizeof(SafetyUniforms)==28);
        safety.capacity=config.maxOutputClusters;
        safety.enableFallback=config.gpuFallbackEnabled ? 1u : 0u;
        safety.firstIndex=config.gpuFallbackFirstIndex;
        safety.indexCount=config.gpuFallbackIndexCount;
        safety.baseVertex=config.gpuFallbackBaseVertex;
        safety.temporalNodeCount=temporal?config.temporalNodeCount:0;
        safety.temporalHasPrevious=warmTemporal?1u:0u;
        if(temporal){
            dev.setComputeBuffer(2,previousTemporal);
            dev.setComputeBuffer(3,nextTemporal);
        }else{
            // Statically used GLSL bindings 2/3 must remain descriptor-
            // valid, although temporalNodeCount=0 prevents any access.
            dev.setComputeBuffer(2,slot.counters);
            dev.setComputeBuffer(3,slot.counters);
        }
        dev.setComputeUniforms(&safety,sizeof(safety));
        const bool safe=trackedDispatch(std::max(
            safety.capacity,safety.temporalNodeCount),temporalStats_.safetyDispatches);
        dev.endCompute();
        if(!safe)return false;
    }
    GpuSelectionSubmission ready{};
    ready.ranges = slot.ranges;
    ready.counters = slot.counters;
    if(vulkan)ready.indirect=slot.indirect;
    ready.capacity = config.maxOutputClusters;
    ready.submittedInstances = uint32_t(instances.size());
    ready.frameSerial = frameSerial;
    ready.submission = submission;
    if(temporal){
        // Commit the next GPU mask only after reset, selection and GPU
        // validity rollback have all been encoded successfully.
        auto it=temporal_.find(temporalKey);
        if(it==temporal_.end())return false;
        auto& record=it->second;
        const size_t stateSlot=size_t(frameSerial%kFramesInFlight);
        record.frame[stateSlot]=frameSerial;
        record.lastFrame=frameSerial;
        record.width=uint32_t(config.width);
        record.height=uint32_t(config.height);
        record.clipDepthZeroToOne=config.clipDepthZeroToOne;
        record.fraction=config.hysteresisFraction;
        ready.temporalApplied=true;
        ready.temporalState=nextTemporal;
        ++temporalStats_.submitted;
        if(!warmTemporal)++temporalStats_.coldStarts;
    }
    ++cursors_[index];
    ++temporalStats_.successfulDispatches;
    temporalStats_.peakSubmissionsPerFrameSlot=std::max(
        temporalStats_.peakSubmissionsPerFrameSlot,cursors_[index]);
    out = ready;
    return true;
}

void GpuClusterCulling::shutdown(gfx::Device& dev) {
    for (auto& frame : slots_) {
        for (auto& slot : frame) {
            if (slot.instances.valid()) dev.destroyBuffer(slot.instances);
            if (slot.ranges.valid()) dev.destroyBuffer(slot.ranges);
            if (slot.counters.valid()) dev.destroyBuffer(slot.counters);
            if (slot.indirect.valid()) dev.destroyBuffer(slot.indirect);
        }
        frame.clear();
    }
    epochs_.fill(0);
    cursors_.fill(0);
    releaseTemporal(dev);
    if (pipeline_.valid()) dev.destroyCompute(pipeline_);
    if (indirectClearPipeline_.valid())
        dev.destroyCompute(indirectClearPipeline_);
    if (indirectSafetyPipeline_.valid())
        dev.destroyCompute(indirectSafetyPipeline_);
    pipeline_ = {};
    indirectClearPipeline_ = {};
    indirectSafetyPipeline_ = {};
}
} // namespace aiverse::render::strata
