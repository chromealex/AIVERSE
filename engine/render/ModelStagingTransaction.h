// Genre-neutral native Model import transaction. Unlike a regular model
// cache insertion, GPU cache residency is not changed by failed authoring.
// A host owns the GPU rollback pool and device lifetime for every transaction.
#pragma once
#include "ModelResourceCache.h"
#include "ModelImageUploadTransaction.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

class ModelStagingTransaction final {
public:
    ModelStagingTransaction(gfx::Device& device,
                            ModelResourceCache& resources,
                            ModelImageRollbackPool& imageRollback,
                            RenderDeviceLifetime::Lease lease={})
        :device_(device),resources_(resources),
         imageRollback_(imageRollback),lease_(std::move(lease)) {
        if(lease_.device()) {
            if(!lease_.validFor(device_))
                throw std::logic_error("Model staging GPU device generation expired");
            resources_.bindResourceDevice(device_,lease_);
        }else resources_.bindResourceDevice(device_);
    }
    ModelStagingTransaction(const ModelStagingTransaction&)=delete;
    ModelStagingTransaction& operator=(const ModelStagingTransaction&)=delete;
    ~ModelStagingTransaction() {
        for(auto& [key,model]:scratch_) {
            (void)key;
            resources_.retireUnpublished(device_,model);
        }
    }
    // Accept a CPU-decoded Model. Ownership enters scratch before any GPU
    // call, so failed or partial vertex/image/morph uploads cannot escape
    // the normal GPU-fenced retirement path.
    Model* stage(std::string key,std::unique_ptr<Model> model) {
        if(committed_)throw std::logic_error("Model staging already committed");
        if(key.empty() || !model || resources_.contains(key) ||
           scratch_.contains(key))
            throw std::invalid_argument("invalid/duplicate staged Model key");
        auto [it,inserted]=scratch_.try_emplace(std::move(key),std::move(model));
        (void)inserted;
        auto* ptr=it->second.get();
        if(!uploadModel(device_,*ptr,false,false))
            throw std::runtime_error("speculative Model GPU upload failed: "+ptr->path);
        return ptr;
    }
    size_t count() const noexcept{return scratch_.size();}
    Model* find(const std::string& key) const noexcept {
        const auto it=scratch_.find(key);
        return it==scratch_.end()?nullptr:it->second.get();
    }
    // No further cache insertions should interleave between prepare and
    // commit on the native presentation owner thread.
    size_t commit() {
        if(committed_)throw std::logic_error("Model staging already committed");
        std::vector<Model*> models;
        models.reserve(scratch_.size());
        for(const auto& [key,model]:scratch_) {
            if(resources_.contains(key))
                throw std::logic_error("Model key became resident: "+key);
            models.push_back(model.get());
        }
        auto shared=resources_.textures()->prepareGpuPromotion(models);
        const size_t count=scratch_.size();
        resources_.publishStagedModels(device_,scratch_);
        auto redundant=shared.commit();
        imageRollback_.retire(device_,std::move(redundant));
        committed_=true;
        return count;
    }
private:
    gfx::Device& device_;
    ModelResourceCache& resources_;
    ModelImageRollbackPool& imageRollback_;
    RenderDeviceLifetime::Lease lease_;
    ModelResourceCache::ModelMap scratch_;
    bool committed_=false;
};

} // namespace aiverse::render
