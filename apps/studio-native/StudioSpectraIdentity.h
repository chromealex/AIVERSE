#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace aiverse::studio {
// Process-wide opaque identities, exactly representable by JSON/JavaScript.
// Never recycle an identity across preview devices, graph swaps or transfers.
inline uint64_t nextSpectraIdentity() {
    static std::atomic<uint64_t> next{1};
    constexpr uint64_t maxSafe=9007199254740991ULL;
    auto id=next.load(std::memory_order_relaxed);
    for(;;) {
        if(id>maxSafe)throw std::overflow_error("SpectraFX identity space exhausted");
        if(next.compare_exchange_weak(id,id+1,std::memory_order_relaxed))return id;
    }
}
}
