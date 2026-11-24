#pragma once
#include <chrono>
#include <cstdint>
#include <vector>

namespace aiverse::studio {
// Render-owner-only transfer storage. An explicit clock makes expiry testable.
struct SpectraTransferLease {
    using Clock=std::chrono::steady_clock;
    using TimePoint=Clock::time_point;
    static constexpr auto timeout=std::chrono::seconds(30);
    uint64_t id=0;
    TimePoint touched{};
    std::vector<uint8_t> bytes;

    void reset() {
        id=0;
        touched={};
        std::vector<uint8_t>().swap(bytes);
    }
    bool expire(TimePoint now,bool queued=false) {
        if(!id||queued||now-touched<timeout)return false;
        reset();
        return true;
    }
};
}
