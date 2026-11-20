// Conservative completion-fenced GI submission for Metal and Vulkan.
// A dispatch only ENCODEs work: it does not mean the probe texture is ready.
// Metal readBufferAsync returns a completion callback after the GPU command buffer;
// Vulkan waits for completedFrameSerial() from a real reused-slot VkFence.
// No callback captures this queue or a raw ECS/renderer pointer (shutdown-safe).
#pragma once
#include "GiGpuSmallScene.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class GiGpuTicketQueue {
public:
    struct Submission {
        uint64_t serial=0;
        bool submitted=false, fenceQueued=false;
        size_t probes=0;
    };
    struct PollStats {
        size_t completed=0, accepted=0, stale=0, aborted=0, pending=0;
    };
    struct Fence {
        std::atomic<bool> ready{false};
        std::atomic<bool> successful{false};
        bool asyncSupported=false;
    };

    GiGpuTicketQueue() = default;
    GiGpuTicketQueue(const GiGpuTicketQueue&) = delete;
    GiGpuTicketQueue& operator=(const GiGpuTicketQueue&) = delete;
    ~GiGpuTicketQueue() { if (dev_ && fence_.valid()) dev_->destroyBuffer(fence_); }

    bool initialize(gfx::Device& device) {
        if (!pending_.empty()) return false; // cannot discard in-flight readers
        if (dev_ && fence_.valid()) dev_->destroyBuffer(fence_);
        dev_=nullptr;
        fence_={};
        const auto backend=device.info().backend;
        if (!device.supportsCompute() ||
            (backend!=gfx::Backend::Metal && backend!=gfx::Backend::Vulkan))
            return false;
        constexpr uint32_t marker=0x47494631u; // GIF1
        fence_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,
                                    sizeof(marker),&marker,"GI GPU completion marker"});
        if (!fence_.valid()) return false;
        dev_=&device;
        nextSerial_=1;
        return true;
    }

    // Must return false until all async and externally completed batches have
    // been polled. Only then can the caller destroy the GPU backend/device.
    bool shutdown() {
        if (!pending_.empty()) return false;
        if (dev_ && fence_.valid()) dev_->destroyBuffer(fence_);
        dev_=nullptr;
        fence_={};
        return true;
    }

    size_t pending() const { return pending_.size(); }
    bool ready() const { return dev_ && fence_.valid(); }

    // One batch at a time: the current GI compute backend reuses one job buffer
    // and writes directly into one atlas; its storage is NOT safe to reuse
    // until the preceding Metal/Vulkan GPU completion fence is confirmed.
    // A future multi-batch backend needs per-submission job buffers/atlas lanes.
    //
    // Critical conservative presentation policy: hide the dynamic overlay
    // if ANY probe is dirty or in flight; an obsolete frame is never presented
    // while the producer is recomputing geometry/light. Future clipmaps can
    // replace this with per-brick valid masks instead of hiding the whole volume.
    template<class Scheduler>
    bool safeToSample(const Scheduler& scheduler) const {
        return ready() && dev_->gpuHealthy() && pending_.empty() &&
               scheduler.pending()==0 && scheduler.inFlight()==0;
    }

    bool setGeometry(GiGpuSmallScene& backend,
                     const GiChunkedGeometrySnapshot& snapshot,uint64_t revision) {
        if (!ready() || !pending_.empty()) return false;
        return backend.setGeometry(snapshot,revision);
    }
    bool setPackedGeometry(GiGpuSmallScene& backend,const GiGpuBvh& packet,
                           uint64_t revision) {
        if(!ready() || !pending_.empty())return false;
        return backend.setPackedGeometry(packet,revision);
    }
    bool setLights(GiGpuSmallScene& backend,std::vector<GiMovingLight> lights) {
        if (!ready() || !pending_.empty()) return false;
        return backend.setLights(std::move(lights));
    }

    template<class Scheduler>
    Submission submit(GiGpuSmallScene& backend,Scheduler& scheduler,
                      size_t probeBudget,float3 focus,uint32_t samples,
                      float traceDistance) {
        Submission result;
        if (!ready() || !dev_->gpuHealthy() || !backend.ready() ||
            !probeBudget || !pending_.empty() ||
            backend.atlas().device()!=dev_)
            return result;
        const auto tickets=scheduler.claim(std::min(probeBudget,GiGpuSmallScene::kMaxJobs),
                                           focus);
        if(tickets.empty())return result;
        const auto dispatch=backend.dispatch(tickets,samples,traceDistance);
        if(!dispatch.submitted) {
            for(const auto& t:tickets) scheduler.abort(t);
            return result;
        }
        Batch batch;
        batch.serial=nextSerial_++;
        batch.tickets=tickets;
        batch.fence=std::make_shared<Fence>();
        const auto state=batch.fence;
        batch.fence->asyncSupported=dev_->info().backend==gfx::Backend::Metal &&
            dev_->readBufferAsync(
            fence_,sizeof(uint32_t),[state](std::vector<uint8_t> bytes) {
                uint32_t marker=0;
                if(bytes.size()==sizeof(marker))std::memcpy(&marker,bytes.data(),sizeof(marker));
                state->successful.store(marker==0x47494631u,std::memory_order_release);
                state->ready.store(true,std::memory_order_release);
            });
        if (dev_->info().backend==gfx::Backend::Vulkan) {
            // The dispatch is encoded in the CURRENT Vulkan frame. Its result
            // must not be published before the VkFence for this exact serial
            // completes; endFrame merely submits the command buffer.
            batch.gpuFrameSerial=dev_->frameSerial();
        }
        result={batch.serial,true,batch.fence->asyncSupported ||
                batch.gpuFrameSerial!=0,tickets.size()};
        pending_.push_back(std::move(batch));
        return result;
    }

    // For RHI backends that reject async readback AFTER the dispatch was encoded:
    // host must obtain a separate, REAL GPU completion signal before calling this.
    // Never call from endFrame() merely because the command buffer was submitted.
    bool confirmExternalCompletion(uint64_t serial,bool success) {
        for(auto& batch:pending_) if(batch.serial==serial &&
            !batch.fence->asyncSupported && batch.gpuFrameSerial==0) {
            batch.fence->successful.store(success,std::memory_order_release);
            batch.fence->ready.store(true,std::memory_order_release);
            return true;
        }
        return false;
    }

    template<class Scheduler>
    PollStats poll(Scheduler& scheduler) {
        PollStats stats;
        for(size_t i=0;i<pending_.size();) {
            auto& batch=pending_[i];
            // Device loss is a terminal failure, not a successful GPU fence.
            // Vulkan may never signal the submitted frame's VkFence after a
            // device loss; Metal's completion callback may also be absent.
            // Abort scheduler tickets now, leaving their probe cells DIRTY,
            // so shutdown/reinitialization cannot hang on an impossible ACK.
            if(dev_ && !dev_->gpuHealthy()) {
                batch.fence->successful.store(false,std::memory_order_release);
                batch.fence->ready.store(true,std::memory_order_release);
            } else if(batch.gpuFrameSerial && dev_ &&
                      dev_->completedFrameSerial()>=batch.gpuFrameSerial) {
                batch.fence->successful.store(true,std::memory_order_release);
                batch.fence->ready.store(true,std::memory_order_release);
            }
            if(!batch.fence->ready.load(std::memory_order_acquire)) {++i;continue;}
            ++stats.completed;
            const bool success=batch.fence->successful.load(std::memory_order_acquire);
            for(const auto& ticket:batch.tickets) {
                if(success) {
                    if(scheduler.complete(ticket)) ++stats.accepted;
                    else ++stats.stale;
                } else {
                    scheduler.abort(ticket); // remains dirty and may retry
                    ++stats.aborted;
                }
            }
            pending_.erase(pending_.begin()+i);
        }
        stats.pending=pending_.size();
        return stats;
    }

private:
    struct Batch {
        uint64_t serial=0;
        std::vector<GiProbeTicket> tickets;
        std::shared_ptr<Fence> fence;
        uint64_t gpuFrameSerial=0; // Vulkan: genuine completed-frame fence, never a submitted-only serial
    };
    gfx::Device* dev_=nullptr; // device owner must outlive queue
    gfx::BufferHandle fence_{};
    uint64_t nextSerial_=1;
    std::vector<Batch> pending_;
};

} // namespace aiverse::render
