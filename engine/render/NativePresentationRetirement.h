// AIVERSE: generic native presentation GPU-resource retirement.
//
// Designed for lightweight scene hosts (roads, shores, previews) that own
// Models and occasional standalone vertex buffers without ModelResourceCache.
// The host calls collect() only at an actual frame boundary, after previous
// frame work was submitted. Enqueue never treats frame age as GPU completion.
// All methods and Device operations are render-owner-thread only.
#pragma once
#include "AsyncResourceRetirementQueue.h"
#include "FailedModelUpload.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

namespace aiverse::render {

class NativePresentationRetirement final {
    struct Resource {
        std::unique_ptr<Model> model;
        gfx::BufferHandle buffer;
        gfx::TextureHandle texture;
    };
    using Queue=AsyncResourceRetirementQueue<Resource>;

public:
    NativePresentationRetirement() : queue_(std::make_unique<Queue>()) {}
    NativePresentationRetirement(const NativePresentationRetirement&)=delete;
    NativePresentationRetirement& operator=(const NativePresentationRetirement&)=delete;

    ~NativePresentationRetirement() {
        // A host that skipped/failed shutdown cannot infer GPU completion.
        // Preserve Model CPU storage and RHI handles until device teardown
        // instead of destructing Model objects still referenced by GPU work.
        if(queue_ && !queue_->empty()) (void)queue_.release();
    }

    size_t pending() const noexcept {return queue_ ? queue_->pending() : 0;}
    size_t orphaned() const noexcept {return orphaned_;}

    // Removal from a draw list must happen before calling retireModel().
    // An allocation failure falls back to a single explicit idle barrier;
    // failure to prove idle intentionally leaks, never releases GPU-live data.
    void retireModel(gfx::Device& device,std::unique_ptr<Model>& model) noexcept {
        if(!model)return;
        if(!bind(device)) {
            (void)model.release();
            ++orphaned_;
            return;
        }
        Resource resource;
        resource.model=std::move(model);
        try {
            queue_->enqueueFrom(resource);
        }catch(...) {
            const auto status=abortFailedModelUpload(device,resource.model);
            if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                ++orphaned_;
        }
    }

    // The native SpriteMesh owner drops its BufferHandle immediately, but
    // Device destruction is delayed behind the same ordered GPU tokens.
    void retireBuffer(gfx::Device& device,gfx::BufferHandle& buffer) noexcept {
        if(!buffer.valid())return;
        if(!bind(device)) {
            buffer={};
            ++orphaned_;
            return;
        }
        Resource resource;
        resource.buffer=std::exchange(buffer,gfx::BufferHandle{});
        try {
            queue_->enqueueFrom(resource);
        }catch(...) {
            try {
                if(device.waitForResourceIdle()) {
                    device.destroyBuffer(resource.buffer);
                    resource.buffer={};
                    return;
                }
            }catch(...) {}
            ++orphaned_; // never free without proof of completed GPU work
        }
    }

    // Same ordered GPU completion as buffers: old map textures may still be
    // sampled by an in-flight secondary camera or render-target capture.
    void retireTexture(gfx::Device& device,gfx::TextureHandle& texture) noexcept {
        if(!texture.valid())return;
        if(!bind(device)) {
            texture={};
            ++orphaned_;
            return;
        }
        Resource resource;
        resource.texture=std::exchange(texture,gfx::TextureHandle{});
        try {
            queue_->enqueueFrom(resource);
        }catch(...) {
            try {
                if(device.waitForResourceIdle()) {
                    device.destroyTexture(resource.texture);
                    resource.texture={};
                    return;
                }
            }catch(...) {}
            ++orphaned_; // keep old GPU-live textures if idle cannot be proved
        }
    }

    // Must be called at the owning host's frame boundary, NOT from a
    // Renderer3D draw callback or while command recording an in-flight draw.
    // Safe when pending()==0. Backends without async tokens retain
    // resources until the queue crosses its advisory backpressure count,
    // then use one explicit full-idle fallback instead of stalling on
    // every minor terrain/weather edit. Shutdown always requires GPU idle.
    bool collect(gfx::Device& device) {
        if(!bind(device))return false;
        if(!queue_ || queue_->empty())return true;
        try {
            return queue_->collect(
                [&]{return device.beginResourceRetirement();},
                [&](uint64_t token){return device.resourceRetirementComplete(token);},
                [&](uint64_t token) {
                    if(device.resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Complete)
                        device.endResourceRetirement(token);
                    else
                        device.abandonRetirementTokenAfterIdle(token);
                },
                [&]{
                    // Unavailable GPU tokens must not cause a full-GPU
                    // stall on each rebuild. Keep pending until a bounded
                    // pressure point (or the explicit shutdown barrier).
                    return queue_->pending()>=kIdleBackpressureCount &&
                           device.waitForResourceIdle();
                },
                [&](Resource& resource){release(device,resource);},
                [&](uint64_t token) {
                    return device.resourceRetirementStatus(token)==
                           gfx::ResourceRetirementStatus::Failed;
                });
        }catch(...) {
            return false;
        }
    }

    // Stage38: nonblocking variant for immutable full-scene replacement.
    // Never invoke the queue's idle fallback on resource pressure: retain
    // the old owner until its exact backend retirement tokens complete.
    // This must run at the render owner frame boundary, not inside a pass.
    bool collectWithoutIdle(gfx::Device& device) {
        if(!bind(device))return false;
        if(!queue_ || queue_->empty())return true;
        try {
            return queue_->collect(
                [&]{return device.beginResourceRetirement();},
                [&](uint64_t token){return device.resourceRetirementComplete(token);},
                [&](uint64_t token) {
                    if(device.resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Complete)
                        device.endResourceRetirement(token);
                    else
                        device.abandonRetirementTokenAfterIdle(token);
                },
                []{return false;}, // never call waitForResourceIdle()
                [&](Resource& resource){release(device,resource);},
                [&](uint64_t token) {
                    return device.resourceRetirementStatus(token)==
                           gfx::ResourceRetirementStatus::Failed;
                });
        }catch(...) {return false;}
    }

    // Explicit shutdown barrier, also covering STILL-ACTIVE models, kit
    // images and any SpriteBatch/TextureCache handles owned by the host.
    // The host must destroy those objects only AFTER this returns true.
    bool completeForShutdown(gfx::Device& device) noexcept {
        if(!bind(device))return false;
        try {
            if(!device.waitForResourceIdle())return false;
            if(queue_ && !queue_->drain(
                    []{return true;},
                    [&](uint64_t token){
                        device.abandonRetirementTokenAfterIdle(token);
                    },
                    [&](Resource& resource){release(device,resource);}))
                return false;
            device_=nullptr;
            owner_={};
            return true;
        }catch(...) {
            return false;
        }
    }

private:
    static constexpr size_t kIdleBackpressureCount=128;
    static void release(gfx::Device& device,Resource& resource) {
        if(resource.model) {
            releaseModel(device,*resource.model);
            resource.model.reset();
        }
        if(resource.buffer.valid()) {
            device.destroyBuffer(resource.buffer);
            resource.buffer={};
        }
        if(resource.texture.valid()) {
            device.destroyTexture(resource.texture);
            resource.texture={};
        }
    }
    bool bind(gfx::Device& device) noexcept {
        const auto thread=std::this_thread::get_id();
        if(!device_) {
            device_=&device;
            owner_=thread;
            return true;
        }
        return device_==&device && owner_==thread;
    }

    std::unique_ptr<Queue> queue_;
    gfx::Device* device_=nullptr;
    std::thread::id owner_;
    size_t orphaned_=0;
};

} // namespace aiverse::render
