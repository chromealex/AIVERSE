// AIVERSE LENS: optional native runtime host for project-authored ECS cameras.
// The game owns scene submission and gfx frame lifecycle; no genre dependency.
#pragma once
#include "../sim/CameraSceneAsset.h"
#include "CameraRenderPipeline.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {
class CameraRuntimeHost {
    World cameras_;
    SceneHierarchy hierarchy_;
    CameraRenderScheduler scheduler_;
    std::unique_ptr<CameraRenderPipeline> pipeline_;
    CameraRigEvaluator rigs_;
    CameraPostFxPool post_;
    std::vector<Entity> entities_;
    gfx::Device* device_=nullptr;
public:
    // A content loader may supply camera JSON bytes from packaged resources,
    // Android assets, network cache, or a normal local filesystem.
    bool loadAsset(const std::string& text,std::string* error=nullptr) {
        if(pipeline_) {
            if(error)*error="Shutdown the previous LENS host after GPU idle before reloading";
            return false;
        }
        try {
            World replacement;
            SceneHierarchy replacementHierarchy;
            std::vector<Entity> loaded;
            if(!loadCameraScene(replacement,replacementHierarchy,Json::parse(text),
                                &loaded,error))return false;
            cameras_=std::move(replacement);
            hierarchy_=std::move(replacementHierarchy);
            entities_=std::move(loaded);
            scheduler_.reset();rigs_.reset();
            return true;
        } catch(const std::exception& e) {
            if(error)*error=e.what();
            return false;
        }
    }
    bool init(gfx::Device& device,Renderer3D& renderer) {
        if(pipeline_||device_)return false;
        if(!post_.init(device))return false;
        device_=&device;
        pipeline_=std::make_unique<CameraRenderPipeline>(device,renderer,scheduler_);
        return true;
    }
    // Call after Simulation/SceneHierarchy updates, between gfx begin/endFrame.
    // Submit must render the project scene into Renderer3D for each view.
    template<class Submit>
    CameraExecutionResult render(uint64_t frame,double seconds,float dt,
                                 Submit&& submit,Vec3 renderOrigin={}) {
        if(!pipeline_)return {};
        hierarchy_.update(cameras_);
        return pipeline_->executeManaged(cameras_,frame,seconds,dt,
            std::forward<Submit>(submit),rigs_,post_,renderOrigin);
    }
    void requestCapture(Entity e) {scheduler_.requestCapture(e);}
    void markDirty(Entity e) {scheduler_.markDirty(e);}
    const std::vector<Entity>& entities()const{return entities_;}
    const World& world()const{return cameras_;}
    CameraRenderPipeline* pipeline(){return pipeline_.get();}
    const CameraRenderPipeline* pipeline()const{return pipeline_.get();}
    CameraPostFxPool& postFx(){return post_;}
    // Must run after device GPU idle, before its lifetime ends.
    void shutdownAfterGpuIdle() {
        if(pipeline_)pipeline_->shutdownAfterGpuIdle();
        if(device_)post_.shutdownAfterGpuIdle();
        pipeline_.reset();device_=nullptr;rigs_.reset();scheduler_.reset();
    }
};
} // namespace aiverse::render
