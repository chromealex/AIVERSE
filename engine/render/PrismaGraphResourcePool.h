#pragma once
// PRISMA Stage 12: GPU-handle-agnostic physical target pool and temporal history
// lifetime policy. The host owns creation/destruction and all RHI transitions.
// One pool instance per camera/view; no gameplay/genre dependencies.
#include "PrismaRenderGraph.h"
#include "PrismaGraphDispatch.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

template <typename Handle>
class PrismaGraphResourcePool {
public:
    struct Entry {
        PrismaGraphFormat format = PrismaGraphFormat::RGBA16F;
        uint32_t width = 0, height = 0;
        Handle handle{};
        std::string historyName;
        bool historyValid = false; // valid CONTENT from a previous frame, not just allocated storage
        bool selected = false;
    };

    struct Result {
        bool success = false;
        std::string error;
        std::vector<Handle> slots;
        std::vector<bool> historyValid; // true only for slots with reusable temporal content
    };

    // Create: Handle(PrismaGraphFormat,uint32_t width,uint32_t height).
    // Release: void(Handle). No GPU operations are done by this class.
    // On allocation failure, every allocation made by this prepare() is released,
    // while previous live allocations remain owned by the pool.
    template <typename Create, typename Release>
    Result prepare(const PrismaRenderGraph& graph, const PrismaGraphPlan& plan,
                   Create&& create, Release&& release) {
        Result result;
        if (!plan.valid || plan.resources.size() != graph.resources().size()) {
            result.error = "PRISMA pool: invalid graph plan";
            return result;
        }
        // Exactly one persistent history owner per physical slot.
        std::vector<std::string> histories(plan.slots.size());
        for (size_t i = 0; i < graph.resources().size(); ++i) {
            const int slot = plan.resources[i].slot;
            if (slot < 0 || graph.resources()[i].external ||
                !graph.resources()[i].history) continue;
            if (static_cast<size_t>(slot) >= histories.size() ||
                !histories[static_cast<size_t>(slot)].empty()) {
                result.error = "PRISMA pool: invalid history slot mapping";
                return result;
            }
            histories[static_cast<size_t>(slot)] = graph.resources()[i].name;
        }
        std::vector<bool> reserved(entries_.size(), false);
        std::vector<Entry> pending;
        std::vector<size_t> chosen;
        result.slots.reserve(plan.slots.size());
        result.historyValid.reserve(plan.slots.size());

        for (size_t i = 0; i < plan.slots.size(); ++i) {
            const auto& slot = plan.slots[i];
            const auto& history = histories[i];
            // Dedicated non-history allocations are still safe to reuse on
            // subsequent frames, but never share the same physical slot in one frame.
            size_t found = entries_.size();
            for (size_t j = 0; j < entries_.size(); ++j) {
                const auto& e = entries_[j];
                if (!reserved[j] && e.format == slot.format &&
                    e.width == slot.width && e.height == slot.height &&
                    e.historyName == history) {
                    found = j;
                    break;
                }
            }
            if (found != entries_.size()) {
                reserved[found] = true;
                chosen.push_back(found);
                result.slots.push_back(entries_[found].handle);
                result.historyValid.push_back(!history.empty() && entries_[found].historyValid);
                continue;
            }
            Handle h = create(slot.format, slot.width, slot.height);
            if (h == Handle{}) {
                for (const auto& p : pending) release(p.handle);
                result.slots.clear();
                result.historyValid.clear();
                result.error = "PRISMA pool: target allocation failed at slot " + std::to_string(i);
                return result;
            }
            pending.push_back(Entry{slot.format, slot.width, slot.height, h, history, false, false});
            chosen.push_back(entries_.size() + pending.size() - 1);
            result.slots.push_back(h);
            result.historyValid.push_back(false);
        }
        // Commit only after every allocation succeeded.
        for (auto& entry : entries_) entry.selected = false;
        entries_.insert(entries_.end(), pending.begin(), pending.end());
        for (auto id : chosen) entries_[id].selected = true;
        result.success = true;
        return result;
    }

    // Called only when a frame was successfully submitted and history outputs
    // were actually written. A failed/aborted frame must NOT mark history valid.
    void markHistoryWritten(const std::string& resourceName) {
        for (auto& entry : entries_)
            if (entry.selected && entry.historyName == resourceName)
                entry.historyValid = true;
    }

    // Camera cuts/teleports or temporal reset invalidate pixels, not storage.
    void invalidateHistory() {
        for (auto& entry : entries_) entry.historyValid = false;
    }

    // Deletes old framebuffer allocations after size changes and graph rebuilds.
    // Call only after GPU submission/fences guarantee resources are no longer in flight.
    template <typename Release>
    void pruneUnused(Release&& release) {
        auto it = entries_.begin();
        while (it != entries_.end()) {
            if (!it->selected) {
                release(it->handle);
                it = entries_.erase(it);
            } else ++it;
        }
    }

    // Transfer unused allocations to an external retirement mechanism instead
    // of destroying them while GPU work could still reference their textures.
    // Retire callback takes Handle; it must attach the correct last-submitted
    // GPU fence before invoking device destruction at completion.
    template <typename Retire>
    void retireUnused(Retire&& retire) {
        auto it = entries_.begin();
        while (it != entries_.end()) {
            if (!it->selected) {
                retire(it->handle);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Transfer all allocations to the host's deferred retirement queue.
    // The caller must ensure the resource completion fence is valid.
    template <typename Retire>
    void retireAll(Retire&& retire) {
        for (auto& entry : entries_) retire(entry.handle);
        entries_.clear();
    }

    // Full teardown after all GPU work referencing these handles has completed.
    template <typename Release>
    void clear(Release&& release) {
        for (auto& entry : entries_) release(entry.handle);
        entries_.clear();
    }

    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

} // namespace aiverse::render
