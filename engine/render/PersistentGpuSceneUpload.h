#pragma once
// Safe, backend-independent per-frame-slot GPU scene synchronization.
// The owner must call sync after Device::beginFrame and before GPU reads.
// This uses dynamic vertex buffers as byte-addressable upload storage; GPU
// compute binding is deliberately separate until backend storage APIs exist.
#include "PersistentGpuScene.h"
#include "engine/gfx/Gfx.h"
#include "GpuOwnedMemory.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>

namespace aiverse::render {
class PersistentGpuSceneUpload {
public:
    static constexpr uint32_t kFrames = 3;
    struct Stats { uint32_t ranges = 0, records = 0; size_t bytes = 0; };
    explicit PersistentGpuSceneUpload(gfx::Device& device) : device_(device) {}
    ~PersistentGpuSceneUpload() { release(); }
    PersistentGpuSceneUpload(const PersistentGpuSceneUpload&) = delete;
    PersistentGpuSceneUpload& operator=(const PersistentGpuSceneUpload&) = delete;

    // frameIndex must correspond to a GPU-completed slot. The owner must
    // synchronize completion; this class does not invent GPU fences.
    Stats sync(const PersistentGpuScene& scene, uint64_t frameIndex) {
        const uint32_t slot = uint32_t(frameIndex % kFrames);
        auto& state = slots_[slot];
        Stats stats{};
        const size_t count = scene.capacity();
        if (count == 0) return stats;
        const size_t required = count * sizeof(GpuSceneRecord);
        if (!state.buffer.valid() || state.capacity < required) {
            if (state.buffer.valid()) device_.destroyBuffer(state.buffer);
            gfx::BufferDesc desc{};
            desc.type = gfx::BufferType::Vertex;
            desc.usage = gfx::Usage::Dynamic;
            desc.size = required;
            desc.label = "persistent gpu scene staging";
            state.buffer = device_.createBuffer(desc);
            if (!state.buffer.valid()) {
                state.capacity = 0;
                state.mirror.clear();
                return stats;
            }
            state.capacity = required;
            state.mirror.clear();
        }
        uint32_t begin = UINT32_MAX;
        const auto upload = [&](uint32_t end) {
            if (begin == UINT32_MAX) return;
            const uint32_t length = end - begin;
            device_.updateBuffer(state.buffer, state.mirror.data() + begin,
                size_t(length) * sizeof(GpuSceneRecord),
                size_t(begin) * sizeof(GpuSceneRecord));
            ++stats.ranges;
            stats.records += length;
            stats.bytes += size_t(length) * sizeof(GpuSceneRecord);
            begin = UINT32_MAX;
        };
        const size_t oldCount = state.mirror.size();
        state.mirror.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            const GpuSceneRecord& current = *scene.slotRecord(i);
            const bool changed = i >= oldCount ||
                std::memcmp(&state.mirror[i], &current, sizeof(current)) != 0;
            if (changed) {
                state.mirror[i] = current;
                if (begin == UINT32_MAX) begin = i;
            } else upload(i);
        }
        upload(uint32_t(count));
        return stats;
    }
    gfx::BufferHandle buffer(uint64_t frameIndex) const {
        return slots_[frameIndex % kFrames].buffer;
    }
    // All three GPU frame slots, not just the most recent upload.
    // CPU mirrors/scene data and physical Metal buffer versions excluded.
    GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        GpuOwnedMemoryCounter counter(device_);
        for (const auto& slot : slots_) counter.buffer(slot.buffer);
        return counter.stats();
    }
    void release() {
        for (auto& slot : slots_) {
            if (slot.buffer.valid()) device_.destroyBuffer(slot.buffer);
            slot = {};
        }
    }
private:
    struct Slot {
        gfx::BufferHandle buffer{};
        size_t capacity = 0;
        std::vector<GpuSceneRecord> mirror;
    };
    gfx::Device& device_;
    std::array<Slot, kFrames> slots_{};
};
} // namespace aiverse::render
