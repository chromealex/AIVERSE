// Device-owner, GPU-fenced retirement for transient offscreen color/depth
// render targets. A resize/closed viewport must not destroy targets that
// previous Metal/Vulkan/GL submissions may still sample or render into.
#pragma once
#include "RenderDeviceLifetime.h"
#include "AsyncResourceRetirementQueue.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace aiverse::render {

class OffscreenTargetRetirement final {
public:
    using Pair=std::array<gfx::TextureHandle,2>;
    using DiagnosticObserver=AsyncResourceRetirementQueue<Pair>::DiagnosticObserver;
    OffscreenTargetRetirement(gfx::Device& device,
                              RenderDeviceLifetime::Lease lease={})
        :device_(&device),lease_(std::move(lease)) {
        if(lease_.device() && !lease_.validFor(device))
            throw std::logic_error("offscreen GPU device generation invalid");
    }
    OffscreenTargetRetirement(const OffscreenTargetRetirement&)=delete;
    OffscreenTargetRetirement& operator=(const OffscreenTargetRetirement&)=delete;

    void setRetirementDiagnosticObserver(DiagnosticObserver observer) {
        queue_.setDiagnosticObserver(std::move(observer));
    }
    void retire(gfx::TextureHandle color,gfx::TextureHandle depth) noexcept {
        if(!color.valid() && !depth.valid())return;
        if(!deviceValid()) {++orphanedPairs_;return;}
        Pair handles{color,depth};
        try {
            queue_.enqueueFrom(handles);
        }catch(...) {
            ++enqueueFailures_;
            try {
                if(device_->waitForResourceIdle())destroy(handles);
                else ++orphanedPairs_;
            }catch(...) {++orphanedPairs_;}
        }
    }
    bool collect() {
        if(!deviceValid())return false;
        return queue_.collect(
            [&]{return device_->beginResourceRetirement();},
            [&](uint64_t token){return device_->resourceRetirementComplete(token);},
            [&](uint64_t token) {
                if(device_->resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    device_->endResourceRetirement(token);
                else device_->abandonRetirementTokenAfterIdle(token);
            },
            [&]{return device_->waitForResourceIdle();},
            [&](Pair& handles){destroy(handles);},
            [&](uint64_t token){
                return device_->resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Failed;
            });
    }
    bool drain() {
        if(!deviceValid())
            return queue_.empty() && orphanedPairs_==0;
        return queue_.drain(
            [&]{return device_->waitForResourceIdle();},
            [&](uint64_t token) {
                if(token)device_->abandonRetirementTokenAfterIdle(token);
            },
            [&](Pair& handles){destroy(handles);});
    }
    size_t pendingPairs() const noexcept{return queue_.pending();}
    uint64_t enqueueFailures() const noexcept{return enqueueFailures_;}
    uint64_t orphanedPairs() const noexcept{return orphanedPairs_;}
private:
    bool deviceValid() const noexcept {
        return device_ && (!lease_.device() || lease_.validFor(*device_));
    }
    void destroy(Pair& handles) {
        for(auto& handle:handles) {
            if(handle.valid())device_->destroyTexture(handle);
            handle={};
        }
    }
    gfx::Device* device_=nullptr;
    RenderDeviceLifetime::Lease lease_;
    AsyncResourceRetirementQueue<Pair> queue_;
    uint64_t enqueueFailures_=0,orphanedPairs_=0;
};

} // namespace aiverse::render
