#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace aiverse {

// Presentation-only queue. A simulation tick is the index before step() increments it.
// In a network match, predicted batches are retained until the input is confirmed.
// Restoring state at tick T invalidates only undelivered batches at T and later.
template<class Event>
class PresentationEventQueue {
public:
    explicit PresentationEventQueue(bool confirmedOnly = false) : confirmedOnly_(confirmedOnly) {}
    void push(uint32_t tick, const Event& event) {
        if (confirmedOnly_) {
            if (tick >= deliveredBefore_) pending_[tick].push_back(event);
        } else immediate_.push_back(event);
    }
    void rewind(uint32_t tick) {
        if (confirmedOnly_) pending_.erase(pending_.lower_bound(tick), pending_.end());
        else immediate_.clear();
    }
    // Exclusive boundary: min(confirmedNext, simulatedNext) for a network session.
    // Never pass confirmed-but-not-yet-simulated ticks, or their later events will be discarded.
    std::vector<Event> drain(uint32_t confirmedNext = 0) {
        std::vector<Event> out;
        if (!confirmedOnly_) { out.swap(immediate_); return out; }
        auto end = pending_.lower_bound(confirmedNext);
        for (auto it = pending_.begin(); it != end; ++it)
            out.insert(out.end(), it->second.begin(), it->second.end());
        pending_.erase(pending_.begin(), end);
        if (confirmedNext > deliveredBefore_) deliveredBefore_ = confirmedNext;
        return out;
    }
private:
    bool confirmedOnly_;
    uint32_t deliveredBefore_ = 0;
    std::vector<Event> immediate_;
    std::map<uint32_t, std::vector<Event>> pending_;
};

} // namespace aiverse
