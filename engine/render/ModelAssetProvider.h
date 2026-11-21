// Native-project model provider: read, own, upload and retire ordinary
// authored model files without RTS/FPS/editor classes. GPU publication and
// retirement are always performed on the device owner thread.
#pragma once
#include "Model.h"
#include "ModelResourceCache.h"
#include "ModelStagingTransaction.h"
#ifndef __EMSCRIPTEN__
#include "AsyncModelCpuQueue.h"
#endif
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

class SceneModelProviderRetirement;

class ModelAssetProvider final {
public:
    explicit ModelAssetProvider(FileReader reader={})
        :reader_(std::move(reader)){}
    ModelAssetProvider(const ModelAssetProvider&)=delete;
    ModelAssetProvider& operator=(const ModelAssetProvider&)=delete;

    void setReader(FileReader reader) {
#ifndef __EMSCRIPTEN__
        cpuQueue_.shutdown(); // old reader/project may be destroyed after this
#endif
        reader_=std::move(reader);
    }
    void setGpuDeviceLease(RenderDeviceLifetime::Lease lease) {
        gpuLease_=std::move(lease);
    }
    ModelResourceCache& resources() noexcept {return resources_;}
    const ModelResourceCache& resources() const noexcept {return resources_;}
    const std::vector<std::string>& warnings() const noexcept {return warnings_;}
    void clearWarnings() {warnings_.clear();}
    Model* find(const std::string& source) {
        return resources_.find(source);
    }
    const Model* find(const std::string& source) const {
        return resources_.find(source);
    }

    // Publish a CPU-prepared, host-validated asset under a fresh cache key.
    // Authoring hosts may compile rigs before this GPU transaction, keeping
    // their previous generation resident until publication succeeds.
    Model* publishPrepared(gfx::Device& device,const std::string& key,
                           std::unique_ptr<Model> model) {
        bindDevice(device);
        if(resources_.activeModelReaders()!=0)
            throw std::logic_error("Model publication attempted during a render read lease");
        if(resources_.publicationBackpressured())
            throw std::runtime_error("Model publication paused by retirement pressure");
        ModelStagingTransaction staging(device,resources_,imageRollback_,gpuLease_);
        staging.stage(key,std::move(model));
        staging.commit();
        return resources_.find(key);
    }

    // Returns a BORROWED pointer stable until invalidation/shutdown.
    // A failed import is cached as a null entry and can be recovered by
    // invalidating that source. Do not call while renderer submissions
    // reference a model whose source is being replaced.
    Model* load(gfx::Device& device,const std::string& source) {
        bindDevice(device);
        if(source.empty())return nullptr;
        if(resources_.contains(source))return resources_.find(source);
        // Backpressure pauses NEW publication only; existing resources
        // and GPU-in-flight retirement leases must not be dropped.
        if(resources_.publicationBackpressured())return nullptr;
        try {
            if(!reader_)throw std::runtime_error("missing model file reader");
            auto model=loadModel(source,reader_,&warnings_,resources_.textures());
            if(!model)throw std::runtime_error("model import returned no Model");
            prepareModelUpload(*model);
            // No Model/shared GPU cache key becomes resident until the
            // scratch import, GPU preflight, and shared promotion succeed.
            ModelStagingTransaction staging(device,resources_,imageRollback_,gpuLease_);
            staging.stage(source,std::move(model));
            staging.commit();
            return resources_.find(source);
        }catch(const std::exception& e) {
            warnings_.push_back(source+": "+e.what());
            // Preserve legacy failed-load tombstone behavior, but partial
            // GPU uploads are retired via the staging transaction first.
            return resources_.insertIfAbsent(source,{});
        }
    }

#ifndef __EMSCRIPTEN__
    using CpuStats=AsyncModelCpuQueue::Stats;
    using Prepared=AsyncModelCpuQueue::Prepared;
    // Split CPU completion from publication for hosts requiring asset/rig
    // validation. A returned Model has never been uploaded to the GPU.
    bool takePrepared(Prepared& prepared) {return cpuQueue_.pollReady(prepared);}
    void cancelAsync(const std::string& source) {cpuQueue_.invalidate(source);}
    void stopAsync() {cpuQueue_.shutdown();}
    CpuStats cpuStats() const noexcept {return cpuQueue_.stats();}
    void setCpuWorkerCount(size_t count) {cpuQueue_.setWorkerCount(count);}
    size_t cpuWorkerCount() const noexcept {return cpuQueue_.workerCount();}
    void setCpuMaxQueued(size_t count) noexcept {
        cpuQueue_.setMaxQueued(count);
    }
    // Background CPU decoding never touches the device or its buffers.
    // A render-thread caller must publishReady() in a later frame.
    bool requestAsync(
            const std::string& source,
            ModelLoadPriority priority=ModelLoadPriority::Visible) {
        if(source.empty() || resources_.contains(source) ||
           resources_.publicationBackpressured())return false;
        return cpuQueue_.request(source,reader_,resources_.textures(),priority);
    }
    // A count cap is hard. The elapsed-time cap is advisory: a single
    // GPU upload may exceed it; never abandon a completed CPU result.
    // budgetMs<=0 disables the wall-time limit.
    size_t publishReady(gfx::Device& device,size_t maxCount=1,
                        double budgetMs=0) {
        bindDevice(device);
        const auto started=std::chrono::steady_clock::now();
        size_t published=0;
        while(published<maxCount && !resources_.publicationBackpressured()) {
            if(published>0 && budgetMs>0) {
                const auto elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                if(elapsed>=budgetMs)break;
            }
            AsyncModelCpuQueue::Prepared prepared;
            if(!cpuQueue_.pollReady(prepared))break;
            for(auto& warning:prepared.warnings)
                warnings_.push_back(std::move(warning));
            if(resources_.contains(prepared.source))continue;
            try {
                if(!prepared.model)
                    throw std::runtime_error("async model import returned no Model");
                ModelStagingTransaction staging(
                    device,resources_,imageRollback_,gpuLease_);
                staging.stage(prepared.source,std::move(prepared.model));
                staging.commit();
            }catch(const std::exception& e) {
                warnings_.push_back(prepared.source+": "+e.what());
                resources_.insertIfAbsent(prepared.source,{});
            }
            ++published;
        }
        return published;
    }
#endif
    bool publicationBackpressured() const {
        return resources_.publicationBackpressured();
    }
    template<class OnDetach>
    size_t invalidate(gfx::Device& device,
                      const std::vector<std::string>& sources,
                      OnDetach&& onDetach) {
#ifndef __EMSCRIPTEN__
        for(const auto& source:sources)cpuQueue_.invalidate(source);
#endif
        return resources_.invalidateSources(device,sources,
                                            std::forward<OnDetach>(onDetach));
    }
    bool collectRetired(gfx::Device& device) {
        bindDevice(device);
        const bool models=resources_.collectRetired(device);
        const bool images=imageRollback_.collect(device);
        return models && images;
    }
    size_t pendingRolledBackImages() const noexcept {
        return imageRollback_.pendingImages();
    }
    void shutdown(gfx::Device& device) {
#ifndef __EMSCRIPTEN__
        cpuQueue_.shutdown(); // CPU source data never outlives texture cache
#endif
        bindDevice(device);
        if(!imageRollback_.drain(device))
            throw std::runtime_error("ModelAssetProvider rollback GPU barrier failed");
        resources_.shutdown(device);
    }
private:
    friend class SceneModelProviderRetirement;
    // Only an owner holding a completed, ordered OUTER scene GPU token
    // can call this. If nested cache/rollback batches exist, the normal
    // device-wide idle barrier remains mandatory for their independent
    // tokens. This method is NOT a public 'skip wait' switch.
    void shutdownAfterCompletedSceneToken(gfx::Device& device) {
#ifndef __EMSCRIPTEN__
        cpuQueue_.shutdown();
#endif
        bindDevice(device);
        if(imageRollback_.pendingImages()!=0 ||
           resources_.pendingRetirements()!=0) {
            shutdown(device);
            return;
        }
        resources_.shutdownAfterCompletedSceneToken(device);
    }
    void bindDevice(gfx::Device& device) {
        if(gpuLease_.device())
            resources_.bindResourceDevice(device,gpuLease_);
        else
            resources_.bindResourceDevice(device);
    }
    RenderDeviceLifetime::Lease gpuLease_;
    ModelImageRollbackPool imageRollback_;
    FileReader reader_;
    ModelResourceCache resources_;
#ifndef __EMSCRIPTEN__
    AsyncModelCpuQueue cpuQueue_;
#endif
    std::vector<std::string> warnings_;
};

} // namespace aiverse::render
