// Genre-neutral, host-owned retirement queue. It never infers GPU completion
// from frame number, wall time, or guessed frames in flight.
// Fence and dispose callbacks execute on the owner thread.
#pragma once
#include <cstddef>
#include <utility>
#include <vector>

namespace aiverse::render {

template<class Resource>
class ResourceRetirementQueue final {
public:
    void enqueue(Resource value) { pending_.push_back(std::move(value)); }
    size_t pending() const noexcept {return pending_.size();}
    bool empty() const noexcept {return pending_.empty();}
    // If the GPU completion barrier fails, no resource is released.
    // The host must retry at a later safe point or abort shutdown.
    template<class Fence,class Destroy>
    bool collect(Fence&& completed,Destroy&& destroy) {
        if(pending_.empty())return true;
        if(!completed())return false;
        for(auto& resource:pending_)destroy(resource);
        pending_.clear();
        return true;
    }
private:
    std::vector<Resource> pending_;
};

} // namespace aiverse::render
