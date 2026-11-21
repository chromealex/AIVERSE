// Explicit host-owned GPU device generation, safe against pointer/address
// reuse after graphics device recreation. The host MUST retire the epoch
// before destroying its gfx::Device and only after all resource caches have
// completed their own successful GPU-safe shutdown.
//
// No GPU work, allocation or implicit device teardown is performed here.
#pragma once
#include "engine/gfx/Gfx.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>

namespace aiverse::render {

class RenderDeviceLifetime final {
    struct State {
        gfx::Device* device=nullptr;
        std::thread::id thread;
        uint64_t generation=0;
        std::atomic<bool> active{true};
    };
public:
    class Lease final {
        friend class RenderDeviceLifetime;
    public:
        Lease()=default;
        bool validFor(const gfx::Device& device) const noexcept {
            const auto state=state_.lock();
            return state && state->active.load(std::memory_order_acquire) &&
                   state->generation==generation_ &&
                   state->device==&device &&
                   state->thread==std::this_thread::get_id();
        }
        bool alive() const noexcept {
            const auto state=state_.lock();
            return state && state->active.load(std::memory_order_acquire) &&
                   state->generation==generation_;
        }
        uint64_t generation() const noexcept{return generation_;}
        gfx::Device* device() const noexcept {return device_;}
        std::weak_ptr<const void> identity() const noexcept {return state_;}
    private:
        Lease(const std::shared_ptr<State>& state)
            :state_(state),generation_(state->generation),device_(state->device) {}
        std::weak_ptr<State> state_;
        uint64_t generation_=0;
        gfx::Device* device_=nullptr;
    };

    explicit RenderDeviceLifetime(gfx::Device& device)
        :state_(std::make_shared<State>()) {
        state_->device=&device;
        state_->thread=std::this_thread::get_id();
        state_->generation=nextGeneration_.fetch_add(1,std::memory_order_relaxed);
    }
    RenderDeviceLifetime(const RenderDeviceLifetime&)=delete;
    RenderDeviceLifetime& operator=(const RenderDeviceLifetime&)=delete;
    RenderDeviceLifetime(RenderDeviceLifetime&&)=delete;
    RenderDeviceLifetime& operator=(RenderDeviceLifetime&&)=delete;
    ~RenderDeviceLifetime(){retire();}

    Lease lease() const noexcept{return Lease(state_);}
    uint64_t generation() const noexcept{return state_->generation;}
    void retire() noexcept {
        state_->active.store(false,std::memory_order_release);
    }
private:
    inline static std::atomic<uint64_t> nextGeneration_{1};
    std::shared_ptr<State> state_;
};

} // namespace aiverse::render
