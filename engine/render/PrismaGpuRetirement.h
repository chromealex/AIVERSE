#pragma once
// PRISMA Stage 18: backend-agnostic GPU resource retirement.
// Completion tokens MUST correspond to completed GPU work (fence/event).
// CPU frame indices and gfx::Device::frameSerial are NOT proof of completion.
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace aiverse::render {

template <typename Handle>
class PrismaGpuRetirementQueue {
public:
    struct Pending {
        Handle handle{};
        uint64_t completionToken = 0;
    };

    // The caller guarantees that submittedToken identifies GPU work that
    // might still be reading/writing handle. Tokens must be monotonic within
    // one GPU device timeline. No destruction takes place in retire().
    void retire(Handle handle, uint64_t submittedToken) {
        if (handle == Handle{}) return;
        pending_.push_back({handle, submittedToken});
    }

    // Called only after the backend reports its last COMPLETED fence/event.
    // Retained resources can be freely referenced by other in-flight frames
    // until their own token is completed.
    template <typename Release>
    size_t collect(uint64_t completedToken, Release&& release) {
        size_t removed = 0;
        auto it = pending_.begin();
        while (it != pending_.end()) {
            if (it->completionToken <= completedToken) {
                release(it->handle);
                it = pending_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    // Only call when the GPU device/queue is known idle (e.g. explicit wait).
    template <typename Release>
    size_t drainAfterGpuIdle(Release&& release) {
        const size_t count = pending_.size();
        for (const auto& p : pending_) release(p.handle);
        pending_.clear();
        return count;
    }

    size_t pendingCount() const { return pending_.size(); }
    bool empty() const { return pending_.empty(); }
    const std::vector<Pending>& pending() const { return pending_; }

private:
    std::vector<Pending> pending_;
};

} // namespace aiverse::render
