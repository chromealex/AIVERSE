// GPU-safe deferred ownership for LumaWeave offscreen render textures.
// The old Luma branch does not expose gfx::Device submission/completion
// fences; on that RHI a zero token is quarantined until genuine GPU idle.
// Never confuse Luma's compute readback marker with completion of the later
// HDR fullscreen draw or material receiver pass.
#pragma once
#include "../gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::render {

template<class DeviceT>
uint64_t lumaWeaveSubmittedFence(const DeviceT& device) {
    if constexpr (requires(const DeviceT& d) { d.gpuSubmissionFence(); })
        return device.gpuSubmissionFence();
    else return 0;
}

template<class DeviceT>
uint64_t lumaWeaveCompletedFence(const DeviceT& device) {
    if constexpr (requires(const DeviceT& d) { d.gpuCompletedFence(); })
        return device.gpuCompletedFence();
    else return 0;
}

// Deliberately bounded: if an RHI has no real timeline, repeatedly changing
// viewport dimensions MUST fail closed instead of growing VRAM indefinitely.
class LumaWeaveTextureRetirement {
public:
    static constexpr size_t kMaxPending=48;
    struct Pending { gfx::TextureHandle handle{}; uint64_t submittedFence=0; };
    size_t pendingCount() const { return pending_.size(); }
    size_t unfencedCount() const {
        size_t result=0;
        for(const auto& p:pending_)if(!p.submittedFence)++result;
        return result;
    }
    bool prepare(size_t additional) {
        if(additional>kMaxPending-pending_.size())return false;
        try { pending_.reserve(pending_.size()+additional); }
        catch(...) { return false; }
        return true;
    }
    // Call prepare(count) before any sequence of retires, so moving a
    // resource handle into quarantine does not allocate or throw.
    void retire(gfx::TextureHandle& handle,uint64_t submissionFence) {
        if(!handle.valid())return;
        pending_.push_back({handle,submissionFence});
        handle={};
    }
    // The caller must supply the device's real contiguous GPU-completed
    // timeline, NEVER frameSerial(), elapsed CPU frames or a compute-only
    // callback. A zero submission cannot be collected this way.
    template<class Release>
    size_t collect(uint64_t completedFence,Release&& release) {
        if(!completedFence)return 0;
        size_t count=0;
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(it->submittedFence && it->submittedFence<=completedFence) {
                release(it->handle);
                it=pending_.erase(it);
                ++count;
            } else ++it;
        }
        return count;
    }
    // Only after the host has proved the ENTIRE graphics queue idle:
    // includes all prior material/scene/composite GPU draws, not just compute.
    template<class Release>
    size_t drainAfterGpuIdle(Release&& release) {
        const size_t count=pending_.size();
        for(const auto& p:pending_)release(p.handle);
        pending_.clear();
        return count;
    }
private:
    std::vector<Pending> pending_;
};


// Tracks graphics use of still-ACTIVE offscreen targets, pipelines and
// shaders. A completed compute marker alone is never a graphics idle proof.
// The first untracked/zero-token submission keeps release blocked until a
// host-proven FULL GPU idle, even if later draws return a valid token.
class LumaWeaveGraphicsUseGate {
public:
    void markUsed(uint64_t submittedFence) {
        if(used_ && (!submittedFence_ || !submittedFence ||
                     submittedFence<submittedFence_))
            submittedFence_=0;
        else submittedFence_=submittedFence;
        used_=true;
    }
    bool canRelease(uint64_t completedFence) const {
        return !used_ || (submittedFence_ && completedFence>=submittedFence_);
    }
    void afterGpuIdle() {
        used_=false;
        submittedFence_=0;
    }
    bool hasUnconfirmedWork() const { return used_; }
private:
    uint64_t submittedFence_=0;
    bool used_=false;
};

} // namespace aiverse::render
