#pragma once
#include "StudioPanelRenderTarget.h"

namespace aiverse::studio {
class StudioPanelModelTarget : protected StudioPanelRenderTarget {
public:
    using StudioPanelRenderTarget::Side;
    ~StudioPanelModelTarget() {
        if(model_) {
            (void)device_->waitForResourceIdle();
            renderer_.invalidateModelCachesFor(model_.get());
            render::releaseModel(*device_,*model_);
        }
    }
    void setModel(std::unique_ptr<render::Model> model){
        if(!model)throw std::runtime_error("No preview model");
        if(!device_->waitForResourceIdle())throw std::runtime_error("Model preview GPU barrier failed");
        if(model_){renderer_.invalidateModelCachesFor(model_.get());render::releaseModel(*device_,*model_);model_.reset();}
        model_=std::move(model);
        model_->batching.mergeRigid=false;model_->batching.autoLod=false;
        model_->batching.geometryPoolPolicy=render::Model::Batching::GeometryPoolPolicy::Dedicated;
        render::prepareModelUpload(*model_);
        ready_=render::uploadModel(*device_,*model_,true,false);
        if(!ready_)throw std::runtime_error("Model preview GPU upload failed");
    }
protected:
    std::unique_ptr<render::Model> model_;
    bool ready_=false;
};
}
