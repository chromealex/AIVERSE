// Request-local scalar scratch identity; never owns a GPU handle.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aiverse::render::texture_graph::gpu {
struct ScalarScratchKey {
    std::uint64_t request=0;
    std::size_t node=0;
    int resolution=0;
    bool vulkan=false;
    bool operator==(const ScalarScratchKey&) const = default;
};
class SharedScalarScratch {
    std::optional<ScalarScratchKey> accepted_,pending_;
public:
    // Called only after the owner confirms scratch storage is still valid,
    // with no outstanding GPU read or submission for this request.
    bool begin(ScalarScratchKey key){
        const bool hit=accepted_&&*accepted_==key;
        if(!hit)accepted_.reset();
        pending_=key;
        return hit;
    }
    // A plan must complete readback and reference parity before reuse.
    void accept(){if(pending_)accepted_=pending_;pending_.reset();}
    void invalidate(){accepted_.reset();pending_.reset();}
};
} // namespace aiverse::render::texture_graph::gpu
