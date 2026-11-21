// AIVERSE generic model-asset publication identity.
// CPU-only; not a GPU fence, renderer frame, ECS generation or gameplay tick.
// Every host that publishes/replaces an asset may use next() to guarantee
// model revision identity survives pointer reuse and registry recreation.
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace aiverse::view {

class ModelPublicationEpoch final {
public:
    // Process-wide monotonic revision. Never returns zero or wraps.
    // Call only after staging the new asset on the publication/owner thread;
    // emit a NEW value whenever a Model is re-imported in place or replaced.
    // These revisions cannot extend the lifetime of a borrowed Model pointer.
    static uint64_t next() {
        uint64_t current=next_.load(std::memory_order_relaxed);
        for(;;) {
            if(current==0 || current==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("Model publication generation exhausted");
            if(next_.compare_exchange_weak(current,current+1,
                    std::memory_order_relaxed,std::memory_order_relaxed))
                return current;
        }
    }
private:
    inline static std::atomic<uint64_t> next_{1};
};

} // namespace aiverse::view
