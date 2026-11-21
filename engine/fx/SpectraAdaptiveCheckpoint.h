// SpectraFX Stage 33 — per-GPU-emitter adaptive checkpoint cadence.
// Render-thread-only scheduling state. No wall-clock timers, gameplay ticks,
// authoritatively replicated state or GPU API calls.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace aiverse::spectra {

struct AdaptiveCheckpointPolicy {
    bool enabled = false;                 // opt in, otherwise fixed cadence
    uint32_t maxIntervalFrames = 240;      // upper bound, never below baseline
    uint32_t recoverAfterCompletions = 3;  // successful completed snapshots
};

class AdaptiveCheckpointCadence final {
public:
    void configure(uint32_t baseFrames,AdaptiveCheckpointPolicy policy) {
        policy_=policy;
        base_=baseFrames;
        current_=baseFrames;
        maximum_=policy.enabled
            ?std::max(base_,policy.maxIntervalFrames):base_;
        successes_=0;backoffs_=recoveries_=0;
        nextDue_=baseFrames;
    }

    bool due(uint64_t submittedGpuRevisions) const {
        return base_!=0 && submittedGpuRevisions>=nextDue_;
    }

    // When a capture is admitted, start counting the next interval. The
    // completion callback may occur later, when the checkpoint is already
    // stale; completion still indicates healthy readback throughput.
    void accepted(uint64_t revision) {
        advanceDue(revision);
    }
    // Busy, budget denial, or RHI enqueue failure: exponential backoff,
    // saturated at configured max, then schedule another attempt.
    void deferred(uint64_t revision) {
        successes_=0;
        if(policy_.enabled && current_<maximum_) {
            current_=current_>maximum_/2?maximum_:current_*2;
            ++backoffs_;
        }
        advanceDue(revision);
    }
    // Only a fully accepted request should reach this callback.
    void completed(bool complete) {
        if(!policy_.enabled || base_==0)return;
        if(!complete) {
            successes_=0;
            if(current_<maximum_) {
                current_=current_>maximum_/2?maximum_:current_*2;
                ++backoffs_;
            }
            return;
        }
        if(current_<=base_)return;
        if(++successes_>=std::max(1u,policy_.recoverAfterCompletions)) {
            current_=std::max(base_,current_/2);
            successes_=0;
            ++recoveries_;
        }
    }

    uint32_t intervalFrames() const {return current_;}
    uint64_t nextDueRevision() const {return nextDue_;}
    uint32_t backoffs() const {return backoffs_;}
    uint32_t recoveries() const {return recoveries_;}

private:
    void advanceDue(uint64_t revision) {
        const uint64_t max=std::numeric_limits<uint64_t>::max();
        nextDue_=revision>max-current_?max:revision+current_;
    }
    AdaptiveCheckpointPolicy policy_{};
    uint32_t base_=0,current_=0,maximum_=0,successes_=0;
    uint32_t backoffs_=0,recoveries_=0;
    uint64_t nextDue_=0;
};

} // namespace aiverse::spectra
