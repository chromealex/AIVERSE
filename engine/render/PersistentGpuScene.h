#pragma once
// Persistent GPU scene staging: stable generation handles, sparse dirty updates,
// and dense active slots. Independent of renderer passes and GPU backend.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace aiverse::render {

struct GpuSceneHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    bool valid() const { return slot != UINT32_MAX && generation != 0; }
};

struct GpuSceneRecord {
    float world[16] = {};
    float boundsMin[4] = {};
    float boundsMax[4] = {};
    uint32_t geometry = 0;
    uint32_t material = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSceneRecord) % 16 == 0);

class PersistentGpuScene {
public:
    struct DirtyRange { uint32_t first = 0, count = 0; };
    GpuSceneHandle insert(const GpuSceneRecord& value) {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (slots_.size() >= UINT32_MAX) return {};
            index = uint32_t(slots_.size());
            slots_.emplace_back();
        }
        auto& slot = slots_[index];
        slot.live = true;
        slot.value = value;
        markDirty(index);
        ++liveCount_;
        return {index, slot.generation};
    }
    bool update(GpuSceneHandle handle, const GpuSceneRecord& value) {
        if (!contains(handle)) return false;
        auto& slot = slots_[handle.slot];
        if (std::memcmp(&slot.value, &value, sizeof(value)) == 0) return true;
        slot.value = value;
        markDirty(handle.slot);
        return true;
    }
    bool erase(GpuSceneHandle handle) {
        if (!contains(handle)) return false;
        auto& slot = slots_[handle.slot];
        slot.live = false;
        slot.value = {};
        if (++slot.generation == 0) ++slot.generation;
        markDirty(handle.slot);
        free_.push_back(handle.slot);
        --liveCount_;
        return true;
    }
    bool contains(GpuSceneHandle handle) const {
        return handle.valid() && handle.slot < slots_.size() &&
            slots_[handle.slot].live && slots_[handle.slot].generation == handle.generation;
    }
    const GpuSceneRecord* get(GpuSceneHandle handle) const {
        return contains(handle) ? &slots_[handle.slot].value : nullptr;
    }
    uint32_t liveCount() const { return liveCount_; }
    // Includes tombstones, so GPU slot indices remain stable after deletion.
    const GpuSceneRecord* slotRecord(uint32_t slot) const {
        return slot < slots_.size() ? &slots_[slot].value : nullptr;
    }
    size_t capacity() const { return slots_.size(); }

    // Flush a coherent CPU snapshot and coalesced dirty ranges. The caller
    // must upload to a GPU buffer that is NOT referenced by an in-flight frame,
    // or use backend-specific per-frame snapshotting. Call after scene edits.
    template<class Upload>
    void flush(Upload&& upload) {
        uint32_t begin = UINT32_MAX;
        for (uint32_t i = 0; i <= slots_.size(); ++i) {
            const bool dirty = i < slots_.size() && slots_[i].dirty;
            if (dirty) {
                if (begin == UINT32_MAX) begin = i;
                slots_[i].dirty = false;
            } else if (begin != UINT32_MAX) {
                staging_.resize(i - begin);
                for (uint32_t j = begin; j < i; ++j)
                    staging_[j - begin] = slots_[j].value;
                upload(DirtyRange{begin, i - begin}, staging_.data());
                begin = UINT32_MAX;
            }
        }
    }
    // A new GPU buffer or recycled frame slot needs a full snapshot, even
    // when no CPU-side scene records changed.
    void invalidateAll() {
        for (auto& slot : slots_) slot.dirty = true;
    }
private:
    struct Slot {
        GpuSceneRecord value{};
        uint32_t generation = 1;
        bool live = false;
        bool dirty = false;
    };
    void markDirty(uint32_t index) { slots_[index].dirty = true; }
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    std::vector<GpuSceneRecord> staging_;
    uint32_t liveCount_ = 0;
};

} // namespace aiverse::render
