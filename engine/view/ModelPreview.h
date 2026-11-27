#pragma once
#include "View.h"
#include "PortraitOptions.h"
#include "engine/render/ModelAssetProvider.h"

namespace aiverse::view {
// One independent presentation subject, usable by HUDs and native asset tools.
// ModelAssetProvider owns GPU resources; View/MotionWeave owns animation state.
class ModelPreview {
public:
    void setReader(render::FileReader reader) { assets_.setReader(std::move(reader)); }
    bool prepare(gfx::Device& device,const PortraitOptions& options,
                 render::float3 team,double time) {
        if(!std::isfinite(time))time=lastTime_;
        if(!failedSource_.empty() && failedSource_==options.model)return false;
        const auto* model=assets_.load(device,options.model);
        if(!model)return false;
        if(source_!=options.model) {
            scene_.clear(); object_={};
            source_=options.model;
            object_.id=1; object_.generation=++generation_;
            object_.animationModelGeneration=generation_;
            object_.draw.model=model;
            // Imported models use their declared idle clip if present; otherwise
            // the first clip is a neutral preview, with bind pose for static assets.
            int clip=model->findClip("Idle");
            if(clip<0)clip=model->findClip("Stand");
            if(clip<0 && !model->clips.empty())clip=0;
            if(clip>=0) {
                auto node=Json::object(); node.set("id",Json::string("preview"));
                node.set("kind",Json::string("clip"));
                node.set("clip",Json::string(model->clips[size_t(clip)].name));
                node.set("loop",Json::fromBool(true));
                auto nodes=Json::array();nodes.push(std::move(node));
                auto graph=Json::object();graph.set("nodes",std::move(nodes));
                graph.set("output",Json::string("preview"));
                try { object_.graph=render::AnimationGraph::compile(*model,graph); }
                catch(const std::exception& error) {
                    failedSource_=options.model;error_=error.what();reset();return false;
                }
            }
            lastTime_=time;
        }
        object_.draw.team=team;
        const float yaw=options.yaw<0 ? float(time)*.6f : options.yaw;
        object_.draw.world=render::mat4::rotateY(yaw);
        const auto extent=model->boundsMax-model->boundsMin;
        const float radius=std::max({extent.x,extent.y,extent.z,.3f})*.6f;
        camera_={};
        camera_.target={(model->boundsMin.x+model->boundsMax.x)*.5f,
            model->boundsMin.y+extent.y*std::clamp(options.focusHeight,0.f,1.f),
            (model->boundsMin.z+model->boundsMax.z)*.5f};
        // Framing follows the rotated subject, including off-origin assets.
        camera_.target=object_.draw.world.transformPoint(camera_.target);
        camera_.distance=radius*3.2f/std::clamp(options.zoom,.25f,8.f);
        camera_.minDistance=.01f;camera_.pitch=.32f;camera_.yaw=.5f;camera_.fovY=.6f;
        const float dt=std::isfinite(time-lastTime_) ? float(std::max(0.0,time-lastTime_)) : 0.f;
        lastTime_=time;
        scene_.update(std::span<const Object>(&object_,1),dt);
        return true;
    }
    const std::string& error() const {return error_;}
    const render::Camera& camera() const {return camera_;}
    void submit(render::Renderer3D& renderer) const {
        scene_.submit(renderer,std::span<const Object>(&object_,1));
    }
    void reset() {scene_.clear();object_={};source_.clear();lastTime_=0;}
    void shutdown(gfx::Device& device) {reset();failedSource_.clear();error_.clear();assets_.shutdown(device);}
private:
    render::ModelAssetProvider assets_;
    View scene_{1};
    Object object_;
    render::Camera camera_;
    std::string source_,failedSource_,error_;
    uint64_t generation_=0;
    double lastTime_=0;
};
} // namespace aiverse::view
