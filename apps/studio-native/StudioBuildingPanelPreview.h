#pragma once
// Independent, demand-driven native Renderer3D preview. No document, main
// Scene camera, drawable or project writes. Only requested frames are rendered.
#include "engine/render/Renderer3D.h"
#include "engine/render/Model.h"
#include "engine/render/RenderDeviceLifetime.h"
#if defined(_WIN32)
#include "engine/gfx/vulkan/Vulkan.h"
#endif
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace aiverse::studio {
class StudioBuildingPanelPreview final {
public:
    static constexpr int Side=512;
    StudioBuildingPanelPreview() {
#if defined(__APPLE__)
        device_=gfx::createMetalDevice(nullptr);
#elif defined(_WIN32)
        gfx::VulkanWindow offscreen;
        std::string error;
        device_=gfx::createVulkanDevice(offscreen,&error);
        if(!device_)throw std::runtime_error("Building offscreen Vulkan device: "+error);
#else
        throw std::runtime_error("Independent Building GPU preview backend unavailable");
#endif
        if(!device_)throw std::runtime_error("Building preview device unavailable");
        lease_=std::make_unique<render::RenderDeviceLifetime>(*device_);
        renderer_.setGpuDeviceLease(lease_->lease());
        if(!renderer_.init(*device_))throw std::runtime_error("Building preview Renderer3D init failed");
        renderer_.setShadowQuality(render::ShadowQuality::Off);
        gfx::TextureDesc color;color.width=color.height=Side;
        color.format=gfx::PixelFormat::RGBA8;color.renderTarget=true;color.label="Building panel preview";
        color_=device_->createTexture(color);
        gfx::TextureDesc depth=color;depth.format=gfx::PixelFormat::Depth;depth.label="Building panel depth";
        depth_=device_->createTexture(depth);
        if(!color_.valid()||!depth_.valid()){
            shutdown();throw std::runtime_error("Building preview render target unavailable");
        }
    }
    ~StudioBuildingPanelPreview(){shutdown();}
    StudioBuildingPanelPreview(const StudioBuildingPanelPreview&)=delete;
    StudioBuildingPanelPreview& operator=(const StudioBuildingPanelPreview&)=delete;
    void setModel(std::unique_ptr<render::Model> model){
        if(!model)throw std::runtime_error("No generated building model");
        if(!device_->waitForResourceIdle())throw std::runtime_error("Building preview GPU barrier failed");
        if(model_){renderer_.invalidateModelCachesFor(model_.get());render::releaseModel(*device_,*model_);model_.reset();}
        model_=std::move(model);
        model_->batching.mergeRigid=false;model_->batching.autoLod=false;
        model_->batching.geometryPoolPolicy=render::Model::Batching::GeometryPoolPolicy::Dedicated;
        render::prepareModelUpload(*model_);
        ready_=render::uploadModel(*device_,*model_,true,false);
        if(!ready_)throw std::runtime_error("Building preview GPU upload failed");
    }
    std::vector<uint8_t> frame(float yaw,float pitch,float zoom,float panX=0.f,float panY=0.f){
        if(!ready_||!model_)throw std::runtime_error("No active Building preview");
        if(!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(zoom)||!std::isfinite(panX)||!std::isfinite(panY)||zoom<.55f||zoom>2.5f||std::abs(panX)>2.f||std::abs(panY)>2.f)
            throw std::runtime_error("Invalid Building preview camera");
        const auto lo=model_->boundsMin,hi=model_->boundsMax,size=hi-lo;
        const float radius=std::max(.05f,.5f*std::sqrt(render::dot(size,size)));
        render::Camera camera;
        camera.target=(lo+hi)*.5f;camera.yaw=std::remainder(yaw,6.2831853f);
        camera.pitch=std::clamp(pitch,-1.35f,1.35f);camera.ortho=false;
        const render::float3 right{std::cos(camera.yaw),0,-std::sin(camera.yaw)};
        const render::float3 up{-std::sin(camera.yaw)*std::sin(camera.pitch),std::cos(camera.pitch),-std::cos(camera.yaw)*std::sin(camera.pitch)};
        camera.target=camera.target+right*(panX*radius)+up*(panY*radius);
        camera.distance=radius/std::sin(camera.fovY*.5f)*1.04f*zoom;
        camera.minDistance=.01f;camera.maxDistance=1e6f;
        camera.zNear=std::max(.01f,camera.distance-radius*2.f);
        camera.zFar=camera.distance+radius*4.f;
        struct Capture{bool done=false;std::vector<uint8_t> pixels;};
        auto capture=std::make_shared<Capture>();
        bool queued=false;
        device_->beginFrame(Side,Side);
        try{
            renderer_.beginScene(camera,Side,Side);
            renderer_.clearDynamicGiOverlay();
            renderer_.lighting=render::Lighting{};
            renderer_.lighting.sunIntensity=1.65f;
            renderer_.lighting.skyColor={.65f,.69f,.76f};
            renderer_.lighting.groundColor={.36f,.34f,.32f};
            renderer_.lighting.fogDensity=0;
            render::ModelDraw draw;draw.model=model_.get();renderer_.drawModel(draw);
            gfx::PassDesc pass;pass.colorTarget=color_;pass.depthTarget=depth_;
            pass.clearColor[0]=.065f;pass.clearColor[1]=.08f;pass.clearColor[2]=.11f;pass.clearColor[3]=1.f;
            renderer_.render(pass);device_->endPass();
            queued=device_->captureTexture(color_,[capture](int w,int h,std::vector<uint8_t> pixels){
                if(w==Side&&h==Side&&pixels.size()==size_t(Side)*Side*4)capture->pixels=std::move(pixels);
                capture->done=true;
            });
        }catch(...){device_->endPass();device_->endFrame();throw;}
        device_->endFrame();
        if(!queued||!device_->waitForSubmittedGpuWork())
            throw std::runtime_error("Building preview GPU readback failed");
        // Metal delivers readbacks on the owner thread after completion.
        for(int i=0;i<3&&!capture->done;++i){
            device_->beginFrame(Side,Side);device_->endFrame();
            if(!device_->waitForSubmittedGpuWork())break;
        }
        if(!capture->done||capture->pixels.empty())throw std::runtime_error("Building preview capture missing");
        return std::move(capture->pixels);
    }
private:
    void shutdown() noexcept {
        if(!device_)return;
        (void)device_->waitForResourceIdle();
        if(model_){renderer_.invalidateModelCachesFor(model_.get());render::releaseModel(*device_,*model_);model_.reset();}
        if(color_.valid())device_->destroyTexture(color_);
        if(depth_.valid())device_->destroyTexture(depth_);
        renderer_.shutdown();if(lease_)lease_->retire();
        device_.reset();
    }
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> lease_;
    render::Renderer3D renderer_;
    std::unique_ptr<render::Model> model_;
    gfx::TextureHandle color_,depth_;
    bool ready_=false;
};
}
