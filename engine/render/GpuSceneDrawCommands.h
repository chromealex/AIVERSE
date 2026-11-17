#pragma once
// Experimental Metal GPU-authored indexed draw arguments.
// A compute pass writes a 20-byte indexed command per eligible opaque model.
// Render pass consumes the generated arguments with no CPU readback.
// This is NOT multi-draw: each eligible model still has one API draw submission.
#include "engine/gfx/Gfx.h"
#include "Math3D.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace aiverse::render {

struct GpuPrimitiveDrawRecord {
    float world[16] = {};
    float boundsMin[4] = {};
    float boundsMax[4] = {};
    gfx::DrawIndexedIndirectCommand command{};
    uint32_t enabled = 0;
    uint32_t padding[2] = {};
    // Optional compatible two-level LOD: command stores near detail and
    // alternate stores far detail, selected entirely by the GPU when enabled.
    gfx::DrawIndexedIndirectCommand lodAlternate{};
    float lodMaxPixels = 0, lodError = 0;
    uint32_t lodEnabled = 0;
    uint32_t lodPadding[4] = {};
};
static_assert(sizeof(GpuPrimitiveDrawRecord) == 176,
    "GpuPrimitiveDrawRecord must match Metal compute layout");
// Dense output record keeps the original model index; GPU ICB encoding will
// consume it in a later, separately validated backend stage.
struct GpuCompactedDrawRecord {
    gfx::DrawIndexedIndirectCommand command{};
    uint32_t sourceIndex = 0;
};
static_assert(sizeof(GpuCompactedDrawRecord) == 24);

class GpuSceneDrawCommands {
public:
    struct UploadStats { uint32_t records = 0, ranges = 0; size_t bytes = 0; };
    GpuSceneDrawCommands() = default;
    GpuSceneDrawCommands(const GpuSceneDrawCommands&) = delete;
    GpuSceneDrawCommands& operator=(const GpuSceneDrawCommands&) = delete;

    bool prepare(gfx::Device& device, const std::vector<GpuPrimitiveDrawRecord>& records,
                 const mat4& viewProjection, uint64_t frameSerial,
                 size_t* submissionOut = nullptr,
                 uint32_t viewportWidth = 0, uint32_t viewportHeight = 0) {
        if (device.info().backend != gfx::Backend::Metal ||
            !device.supportsCompute() || !frameSerial || records.empty() ||
            records.size() > UINT32_MAX) return false;
        if (!pipeline_.valid()) {
            pipeline_ = device.createCompute({kMetalSource, nullptr,
                                              "GPU indexed draw command generator"});
            if (!pipeline_.valid()) return false;
        }
        const size_t frameSlot = frameSerial % slots_.size();
        if (frameEpoch_[frameSlot] != frameSerial) {
            // Metal's frame semaphore has retired this slot before reuse.
            // Multiple Scene/Game render() calls in the SAME frame must never
            // overwrite buffers referenced by earlier encoded GPU draws.
            frameEpoch_[frameSlot] = frameSerial;
            frameCursor_[frameSlot] = 0;
        }
        const size_t submission = frameCursor_[frameSlot]++;
        auto& pool = slots_[frameSlot];
        if (submission == pool.size()) pool.emplace_back();
        Slot& slot = pool[submission];
        const size_t inputBytes = records.size() * sizeof(GpuPrimitiveDrawRecord);
        const size_t outputBytes = records.size() * sizeof(gfx::DrawIndexedIndirectCommand);
        static const bool compact = [] {
            const char* value = std::getenv("AIVERSE_GPU_COMPACT_DRAW_RECORDS");
            return value && std::strcmp(value, "1") == 0;
        }();
        const bool resizedInput = !slot.input.valid() || slot.inputCapacity < inputBytes;
        if (!allocate(device, slot.input, slot.inputCapacity, inputBytes,
                      "GPU draw record input") ||
            !allocate(device, slot.output, slot.outputCapacity, outputBytes,
                      "GPU authored draw arguments")) return false;
        // A recycled frame-slot retains its last CPU/GPU records. Upload only
        // changed consecutive ranges; camera changes still rerun compute.
        // Keep the comparison independent per frame slot for in-flight safety.
        slot.upload = {};
        if (resizedInput) slot.lastRecords.clear();
        const size_t oldCount = slot.lastRecords.size();
        slot.lastRecords.resize(records.size());
        size_t begin = records.size();
        auto flush = [&](size_t end) {
            if (begin == records.size()) return;
            const size_t length = end - begin;
            const size_t bytes = length * sizeof(GpuPrimitiveDrawRecord);
            device.updateBuffer(slot.input, records.data() + begin, bytes,
                                begin * sizeof(GpuPrimitiveDrawRecord));
            slot.upload.ranges++;
            slot.upload.records += uint32_t(length);
            slot.upload.bytes += bytes;
            begin = records.size();
        };
        for (size_t i = 0; i < records.size(); ++i) {
            const bool dirty = i >= oldCount ||
                std::memcmp(&slot.lastRecords[i], &records[i],
                            sizeof(GpuPrimitiveDrawRecord)) != 0;
            if (dirty) {
                slot.lastRecords[i] = records[i];
                if (begin == records.size()) begin = i;
            } else flush(i);
        }
        flush(records.size());
        // Bind all declared compute resources even when compaction is off:
        // Metal validates kernel argument buffers before executing branches.
        if (!allocate(device, slot.compacted, slot.compactedCapacity,
                      compact ? records.size() * sizeof(GpuCompactedDrawRecord)
                              : sizeof(GpuCompactedDrawRecord),
                      "GPU dense visible draw records") ||
            !allocate(device, slot.visibleCount, slot.visibleCountCapacity,
                      sizeof(uint32_t), "GPU visible draw count")) return false;
        if (compact) {
            const uint32_t zero = 0;
            device.updateBuffer(slot.visibleCount, &zero, sizeof(zero));
        }
        slot.compactionActive = false;
        if (!device.beginCompute(pipeline_)) return false;
        device.setComputeBuffer(0, slot.input);
        device.setComputeBuffer(1, slot.output);
        device.setComputeBuffer(2, slot.compacted);
        device.setComputeBuffer(3, slot.visibleCount);
        struct alignas(16) Uniforms {
            float viewProjection[16];
            uint32_t count = 0;
            uint32_t padding[3] = {};
            uint32_t viewportWidth = 0, viewportHeight = 0;
            uint32_t unused[2] = {};
        } params{};
        std::memcpy(params.viewProjection, viewProjection.m, sizeof(params.viewProjection));
        params.count = uint32_t(records.size());
        params.padding[0] = compact ? 1u : 0u;
        params.viewportWidth = viewportWidth;
        params.viewportHeight = viewportHeight;
        device.setComputeUniforms(&params, sizeof(params));
        device.dispatch(params.count);
        device.endCompute();
        slot.commandCount = params.count;
        slot.compactionActive = compact;
        if (submissionOut) *submissionOut = submission;
        return true;
    }

    bool compactedReady(uint64_t frameSerial, size_t submission = 0) const {
        const Slot* slot = find(frameSerial, submission);
        return slot && slot->compactionActive && slot->commandCount > 0 &&
               slot->compacted.valid() && slot->visibleCount.valid();
    }
    gfx::BufferHandle compacted(uint64_t frameSerial, size_t submission = 0) const {
        const Slot* slot = find(frameSerial, submission);
        return slot ? slot->compacted : gfx::BufferHandle{};
    }
    gfx::BufferHandle visibleCount(uint64_t frameSerial, size_t submission = 0) const {
        const Slot* slot = find(frameSerial, submission);
        return slot ? slot->visibleCount : gfx::BufferHandle{};
    }
    UploadStats uploadStats(uint64_t frameSerial, size_t submission = 0) const {
        const Slot* slot = find(frameSerial, submission);
        return slot ? slot->upload : UploadStats{};
    }
    gfx::BufferHandle output(uint64_t frameSerial, size_t submission = 0) const {
        const Slot* slot = find(frameSerial, submission);
        return slot ? slot->output : gfx::BufferHandle{};
    }
    void release(gfx::Device& device) {
        for (auto& pool : slots_) {
            for (Slot& slot : pool) {
                if (slot.input.valid()) device.destroyBuffer(slot.input);
                if (slot.output.valid()) device.destroyBuffer(slot.output);
                if (slot.compacted.valid()) device.destroyBuffer(slot.compacted);
                if (slot.visibleCount.valid()) device.destroyBuffer(slot.visibleCount);
            }
            pool.clear();
        }
        frameEpoch_.fill(0);
        frameCursor_.fill(0);
        if (pipeline_.valid()) device.destroyCompute(pipeline_);
        pipeline_ = {};
    }
private:
    struct Slot {
        gfx::BufferHandle input{}, output{}, compacted{}, visibleCount{};
        size_t inputCapacity = 0, outputCapacity = 0;
        size_t compactedCapacity = 0, visibleCountCapacity = 0;
        uint32_t commandCount = 0;
        std::vector<GpuPrimitiveDrawRecord> lastRecords;
        UploadStats upload{};
        bool compactionActive = false;
    };
    std::array<std::vector<Slot>, 3> slots_{};
    std::array<uint64_t, 3> frameEpoch_{};
    std::array<size_t, 3> frameCursor_{};
    gfx::ComputeHandle pipeline_{};
    const Slot* find(uint64_t frame, size_t submission) const {
        const size_t frameSlot = frame % slots_.size();
        if (!frame || frameEpoch_[frameSlot] != frame ||
            submission >= frameCursor_[frameSlot] ||
            submission >= slots_[frameSlot].size()) return nullptr;
        return &slots_[frameSlot][submission];
    }

    static bool allocate(gfx::Device& device, gfx::BufferHandle& handle,
                         size_t& capacity, size_t required, const char* label) {
        if (handle.valid() && capacity >= required) return true;
        gfx::BufferDesc desc{};
        desc.type = gfx::BufferType::Storage;
        desc.usage = gfx::Usage::Immutable;
        desc.size = required;
        desc.label = label;
        const gfx::BufferHandle replacement = device.createBuffer(desc);
        if (!replacement.valid()) return false;
        if (handle.valid()) device.destroyBuffer(handle);
        handle = replacement;
        capacity = required;
        return true;
    }

    static constexpr const char* kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct DrawArgs {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint firstInstance;
};
struct alignas(16) Record {
    float4 c0, c1, c2, c3;
    float4 lo, hi;
    DrawArgs draw;
    uint enabled;
    uint2 padding;
    DrawArgs lodAlternate;
    float lodMaxPixels;
    float lodError;
    uint lodEnabled;
    uint4 lodPadding;
};
struct Params {
    float4x4 viewProjection;
    uint count;
    uint compactEnabled, pad1, pad2;
    uint viewportWidth, viewportHeight, unused0, unused1;
};
struct CompactedDraw {
    DrawArgs args;
    uint sourceIndex;
};
kernel void cs_main(device const Record* records [[buffer(0)]],
                    device DrawArgs* output [[buffer(1)]],
                    device CompactedDraw* dense [[buffer(2)]],
                    device atomic_uint* visibleCount [[buffer(3)]],
                    constant Params& params [[buffer(8)]],
                    uint idx [[thread_position_in_grid]]) {
    if (idx >= params.count) return;
    const Record r = records[idx];
    DrawArgs args = r.draw;
    if (r.enabled == 0 || args.indexCount == 0) {
        args.instanceCount = 0;
        output[idx] = args;
        return;
    }
    const float4x4 clip = params.viewProjection *
        float4x4(r.c0, r.c1, r.c2, r.c3);
    uint allOutside = 63;
    for (uint corner = 0; corner < 8; ++corner) {
        const float3 p = float3(
            (corner & 1) ? r.hi.x : r.lo.x,
            (corner & 2) ? r.hi.y : r.lo.y,
            (corner & 4) ? r.hi.z : r.lo.z);
        const float4 v = clip * float4(p, 1.0);
        uint outside = 0;
        if (v.x < -v.w) outside |= 1;
        if (v.x > v.w) outside |= 2;
        if (v.y < -v.w) outside |= 4;
        if (v.y > v.w) outside |= 8;
        if (v.z < 0.0) outside |= 16;
        if (v.z > v.w) outside |= 32;
        allOutside &= outside;
    }
    // Match the CPU geometryErrorPixels projection metric for the strict
    // two-level, single-primitive LOD subset. Any near-plane crossing,
    // invalid denominator or projected-size uncertainty keeps full detail.
    if (r.lodEnabled != 0 && params.viewportWidth != 0 &&
        params.viewportHeight != 0 && r.lodMaxPixels > 0 &&
        allOutside == 0) {
        float minW = 1.0e30f, maxAbsX = 0.0f, maxAbsY = 0.0f;
        bool safe = true;
        for (uint corner = 0; corner < 8; ++corner) {
            const float3 p = float3(
                (corner & 1) ? r.hi.x : r.lo.x,
                (corner & 2) ? r.hi.y : r.lo.y,
                (corner & 4) ? r.hi.z : r.lo.z);
            const float4 q = clip * float4(p, 1.0);
            if (!all(isfinite(q)) || q.w <= 0.0f || q.z < 0.0f) {
                safe = false;
                break;
            }
            minW = min(minW, q.w);
            maxAbsX = max(maxAbsX, abs(q.x));
            maxAbsY = max(maxAbsY, abs(q.y));
        }
        const float xNorm = length(float3(clip[0][0], clip[1][0], clip[2][0]));
        const float yNorm = length(float3(clip[0][1], clip[1][1], clip[2][1]));
        const float wNorm = length(float3(clip[0][3], clip[1][3], clip[2][3]));
        const float deltaW = wNorm * r.lodError;
        if (safe && minW > deltaW) {
            const float pixels = 0.5f * max(
                float(params.viewportWidth) * r.lodError *
                    (xNorm * minW + maxAbsX * wNorm),
                float(params.viewportHeight) * r.lodError *
                    (yNorm * minW + maxAbsY * wNorm)) /
                    (minW * (minW - deltaW));
            if (isfinite(pixels) && pixels < r.lodMaxPixels)
                args = r.lodAlternate;
        }
    }
    args.instanceCount = (allOutside == 0) ? args.instanceCount : 0;
    output[idx] = args;
    if (params.compactEnabled != 0 && args.instanceCount != 0) {
        const uint position = atomic_fetch_add_explicit(
            visibleCount, 1u, memory_order_relaxed);
        CompactedDraw packed{};
        packed.args = args;
        packed.sourceIndex = idx;
        dense[position] = packed;
    }
}
)MSL";
};

} // namespace aiverse::render
