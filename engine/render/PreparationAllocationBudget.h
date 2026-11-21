// Owner-thread admission limit for unpublished GPU allocation requests.
// Counts logical requested payload, not backend allocation overhead or VRAM.
#pragma once
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace aiverse::render {
class PreparationAllocationBudget final {
    struct State { size_t limit, used=0; };
public:
    class Lease final {
    public:
        explicit Lease(std::shared_ptr<State> state):state_(std::move(state)){}
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        ~Lease() { state_->used-=bytes_; }
        // Reserve before calling the backend. Failed allocations keep their
        // conservative charge until the entire candidate is discarded.
        void reserve(size_t count,size_t stride=1) {
            const size_t available=state_->limit-state_->used;
            if(stride && count>available/stride)
                throw std::length_error("unpublished GPU preparation budget exceeded");
            const size_t bytes=count*stride;
            state_->used+=bytes;bytes_+=bytes;
        }
    private:
        std::shared_ptr<State> state_;
        size_t bytes_=0;
    };
    explicit PreparationAllocationBudget(size_t limit)
        :state_(std::make_shared<State>(State{limit})){}
    std::shared_ptr<Lease> acquire() {return std::make_shared<Lease>(state_);}
    void setLimit(size_t limit) {
        if(limit<state_->used)
            throw std::logic_error("preparation budget below outstanding leases");
        state_->limit=limit;
    }
    size_t used() const noexcept {return state_->used;}
    size_t limit() const noexcept {return state_->limit;}
private:
    std::shared_ptr<State> state_;
};
} // namespace aiverse::render
