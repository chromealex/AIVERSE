// TerraWeave: host-side frame timing bridge for native RTS/FPS clients.
// Uses the shared gfx::Device GPU timing router and measured CPU wall time.
// Never drains other subscribers' results, invents GPU data or stalls a frame.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>

namespace aiverse::terraweave {

class HorizonFrameTimingBridge {
public:
    using Clock=std::chrono::steady_clock;

    // Called after gfx::Device::beginFrame() and before its endFrame().
    // The caller retains the REAL CPU frame start timestamp for finish().
    // Return false when the optional horizon/adaptation is absent.
    bool begin(gfx::Device& device, world::WorldScene* scene) {
        if(!scene || !scene->vegetationHorizonAdaptiveQualityConfig().enabled) {
            detach(device);
            return false;
        }
        if(capturing_)return false;
        if(!subscriber_)
            subscriber_=device.gpuTimingRouter().subscribe();
        collectGpu(device);
        deliver(*scene,&device);
        ++frameId_;
        Frame pending;
        pending.id=frameId_;
        pending.gpuRequested=subscriber_ &&
            device.gpuTimingRouter().request(device,subscriber_,frameId_);
        frames_.push_back(pending);
        capturing_=true;
        return true;
    }

    // Call once after the host has finished submitting this frame's
    // rendering. The CPU timer is measured (not the clamped sim 'dt').
    // GPU results are paired later by their original tagged frame ID.
    void finish(world::WorldScene* scene,Clock::time_point frameStarted) {
        if(!capturing_)return;
        capturing_=false;
        if(!scene || frames_.empty()){discard();return;}
        const auto micros=std::chrono::duration_cast<
            std::chrono::microseconds>(Clock::now()-frameStarted).count();
        Frame& frame=frames_.back();
        frame.cpuUs=micros>0 && micros<=10000000 ?
            static_cast<uint32_t>(micros) : 0u;
        frame.cpuReady=true;
        deliver(*scene);
    }

    // Does not reset monotonically assigned IDs; old GPU tags can never
    // accidentally match a newly enabled WorldScene/controller.
    void discard() {
        frames_.clear();
        capturing_=false;
    }

    // Explicit teardown if the client is destroyed while the Device remains
    // alive. The router discards only this bridge's outstanding requests.
    void detach(gfx::Device& device) {
        discard();
        if(subscriber_)
            device.gpuTimingRouter().unsubscribe(subscriber_);
        subscriber_=0;
    }
    uint64_t lastFrameId() const {return frameId_;}
    size_t pendingFrames() const {return frames_.size();}
    uint64_t deliveredGpuFrames() const {return deliveredGpuFrames_;}
    uint64_t deliveredCpuOnlyFrames() const {return deliveredCpuOnlyFrames_;}

private:
    struct Frame {
        uint64_t id=0;
        uint32_t cpuUs=0,gpuUs=0;
        bool gpuRequested=false,gpuReady=false,cpuReady=false;
    };
    static constexpr uint64_t kMaxGpuLagFrames=8;
    void collectGpu(gfx::Device& device) {
        auto& router=device.gpuTimingRouter();
        // Only the router drains the Device; other subscribers keep their
        // independent mailboxes even when this bridge polls first.
        router.poll(device);
        if(!subscriber_)return;
        for(const auto& timing:router.take(subscriber_)) {
            for(auto& frame:frames_) {
                if(frame.id!=timing.tag || !frame.gpuRequested ||
                   frame.gpuReady)continue;
                frame.gpuReady=true;
                if(timing.valid && std::isfinite(timing.milliseconds) &&
                   timing.milliseconds>0.0 && timing.milliseconds<=10000.0) {
                    const auto rounded=std::llround(
                        timing.milliseconds*1000.0);
                    if(rounded>0 && rounded<=10000000)
                        frame.gpuUs=static_cast<uint32_t>(rounded);
                }
                break;
            }
        }
    }
    void deliver(world::WorldScene& scene,gfx::Device* device=nullptr) {
        while(!frames_.empty()) {
            const auto& first=frames_.front();
            if(!first.cpuReady)break;
            if(first.gpuRequested && !first.gpuReady &&
               frameId_-first.id<kMaxGpuLagFrames)
                break;
            // CPU-only fallback on unsupported/timed-out GPU providers.
            // Emit each frame ID only once, strictly in increasing order.
            const bool gpuValid=first.gpuReady && first.gpuUs!=0;
            // Reclaim the router's request budget after the eight-frame
            // timeout; the backend result may arrive later but is ignored.
            if(device && subscriber_ && first.gpuRequested &&
               !first.gpuReady)
                device->gpuTimingRouter().cancel(subscriber_,first.id);
            scene.submitVegetationHorizonFrameTiming({
                first.id,first.cpuUs,gpuValid?first.gpuUs:0u});
            if(gpuValid)++deliveredGpuFrames_;
            else ++deliveredCpuOnlyFrames_;
            frames_.pop_front();
        }
    }
    uint64_t frameId_=0;
    gfx::GpuTimingRouter::Subscriber subscriber_=0;
    bool capturing_=false;
    std::deque<Frame> frames_;
    uint64_t deliveredGpuFrames_=0,deliveredCpuOnlyFrames_=0;
};
} // namespace aiverse::terraweave
