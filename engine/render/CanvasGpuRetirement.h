// Genre-independent GPU-fenced owner for canvas, paint and transient draw resources.
// Retire during an encoder submission; collect ONLY at the host's next safe
// frame boundary, after the previous frame's commands have been submitted.
// Pending/Failed completion markers never authorize GPU resource destruction.
#pragma once
#include "RenderDeviceLifetime.h"
#include "AsyncResourceRetirementQueue.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace aiverse::render {

class CanvasGpuRetirement final {
public:
    using Resource=std::variant<gfx::BufferHandle,gfx::TextureHandle,
                                gfx::PipelineHandle,gfx::ShaderHandle>;
    using Stats=AsyncResourceRetirementQueue<Resource>::Stats;

    explicit CanvasGpuRetirement(gfx::Device& device,
                                 RenderDeviceLifetime::Lease lease={})
        :device_(&device),lease_(std::move(lease)) {
        if(lease_.device() && !lease_.validFor(device))
            throw std::logic_error("CanvasGpuRetirement: expired GPU device generation");
    }
    CanvasGpuRetirement(const CanvasGpuRetirement&)=delete;
    CanvasGpuRetirement& operator=(const CanvasGpuRetirement&)=delete;

    void retire(gfx::BufferHandle h) noexcept { if(h.valid())enqueue(Resource{h}); }
    void retire(gfx::TextureHandle h) noexcept { if(h.valid())enqueue(Resource{h}); }
    void retire(gfx::PipelineHandle h) noexcept { if(h.valid())enqueue(Resource{h}); }
    void retire(gfx::ShaderHandle h) noexcept { if(h.valid())enqueue(Resource{h}); }

    bool validFor(const gfx::Device& device) const noexcept {
        return device_==&device && deviceValid();
    }
    size_t pending() const noexcept {return queue_.pending();}
    Stats stats() const noexcept {return queue_.stats();}
    uint64_t orphans() const noexcept{return orphanedResources_;}
    uint64_t enqueueFailures() const noexcept{return enqueueFailures_;}

    bool collect() {
        if(!deviceValid())return queue_.empty() && orphanedResources_==0;
        return queue_.collect(
            [&]{return device_->beginResourceRetirement();},
            [&](uint64_t token){return device_->resourceRetirementComplete(token);},
            [&](uint64_t token) {
                if(device_->resourceRetirementStatus(token)==gfx::ResourceRetirementStatus::Complete)
                    device_->endResourceRetirement(token);
                else device_->abandonRetirementTokenAfterIdle(token);
            },
            [&]{return device_->waitForResourceIdle();},
            [&](Resource& resource){destroy(resource);},
            [&](uint64_t token) {
                return device_->resourceRetirementStatus(token)==gfx::ResourceRetirementStatus::Failed;
            });
    }
    // Only the owner-host shutdown path may block on a complete-device idle.
    bool drain() {
        if(!deviceValid())return queue_.empty() && orphanedResources_==0;
        const bool drained=queue_.drain(
            [&]{return device_->waitForResourceIdle();},
            [&](uint64_t token) {
                if(token)device_->abandonRetirementTokenAfterIdle(token);
            },
            [&](Resource& resource){destroy(resource);});
        if(!drained)return false;
        // A failed enqueue may have left untracked native handles behind.
        // Their destruction is impossible now; after a REAL full idle,
        // the host may safely retire the Device epoch and let the backend
        // clean them up. Preserve the orphan diagnostic count.
        return orphanedResources_==0 || device_->waitForResourceIdle();
    }
private:
    bool deviceValid() const noexcept {
        return device_ && (!lease_.device() || lease_.validFor(*device_));
    }
    void destroy(Resource& resource) {
        std::visit([&](auto& handle) {
            using Handle=std::decay_t<decltype(handle)>;
            if(!handle.valid())return;
            if constexpr(std::is_same_v<Handle,gfx::BufferHandle>)
                device_->destroyBuffer(handle);
            else if constexpr(std::is_same_v<Handle,gfx::TextureHandle>)
                device_->destroyTexture(handle);
            else if constexpr(std::is_same_v<Handle,gfx::PipelineHandle>)
                device_->destroyPipeline(handle);
            else
                device_->destroyShader(handle);
            handle={};
        },resource);
    }
    void enqueue(Resource resource) noexcept {
        if(!deviceValid()) {++orphanedResources_;return;}
        try {
            queue_.enqueueFrom(resource);
        }catch(...) {
            ++enqueueFailures_;
            // OOM fallback only: cannot abandon an in-flight resource
            // by immediately destroying it when enqueue allocation fails.
            try {
                if(device_->waitForResourceIdle())destroy(resource);
                else ++orphanedResources_;
            }catch(...) {++orphanedResources_;}
        }
    }
    gfx::Device* device_=nullptr;
    RenderDeviceLifetime::Lease lease_;
    AsyncResourceRetirementQueue<Resource> queue_;
    uint64_t orphanedResources_=0,enqueueFailures_=0;
};

} // namespace aiverse::render
