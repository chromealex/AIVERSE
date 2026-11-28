#pragma once
// Independent, demand-driven native Renderer3D preview. No document, main
// Scene camera, drawable or project writes. Only requested frames are rendered.
#include "engine/render/Renderer3D.h"
#include "engine/render/Model.h"
#include "engine/render/RenderDeviceLifetime.h"
#if defined(AIVERSE_WITH_VULKAN)
#include "engine/gfx/vulkan/Vulkan.h"
#endif
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace aiverse::studio {
class StudioPanelRenderTarget {
    static std::unique_ptr<gfx::Device> defaultDevice() {
#if defined(__APPLE__)
        return gfx::createMetalDevice(nullptr);
#elif defined(AIVERSE_WITH_VULKAN)
        gfx::VulkanWindow offscreen;
        std::string error;
        auto device=gfx::createVulkanDevice(offscreen,&error);
        if(!device)throw std::runtime_error("Panel offscreen Vulkan device: "+error);
        return device;
#else
        throw std::runtime_error("Independent panel GPU preview backend unavailable");
#endif
    }
public:
    static constexpr int Side=512;
    StudioPanelRenderTarget():StudioPanelRenderTarget(defaultDevice()) {}
    explicit StudioPanelRenderTarget(std::unique_ptr<gfx::Device> device):device_(std::move(device)) {
        if(!device_)throw std::runtime_error("Panel preview device unavailable");
        lease_=std::make_unique<render::RenderDeviceLifetime>(*device_);
        renderer_.setGpuDeviceLease(lease_->lease());
        if(!renderer_.init(*device_))throw std::runtime_error("Panel preview Renderer3D init failed");
        renderer_.setShadowQuality(render::ShadowQuality::Off);
        gfx::TextureDesc color;color.width=color.height=Side;
        color.format=gfx::PixelFormat::RGBA8;color.renderTarget=true;color.label="Panel color";
        color_=device_->createTexture(color);
        gfx::TextureDesc depth=color;depth.format=gfx::PixelFormat::Depth;depth.label="Panel depth";
        depth_=device_->createTexture(depth);
        if(!color_.valid()||!depth_.valid()){
            shutdown();throw std::runtime_error("Panel preview render target unavailable");
        }
    }
    ~StudioPanelRenderTarget(){shutdown();}
    StudioPanelRenderTarget(const StudioPanelRenderTarget&)=delete;
    StudioPanelRenderTarget& operator=(const StudioPanelRenderTarget&)=delete;
    gfx::Device& device() { return *device_; }
    auto lease() const { return lease_->lease(); }

    template<class Draw>
    std::vector<uint8_t> captureFrame(const render::Camera& camera, Draw&& draw) {
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
            draw(renderer_);
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
            throw std::runtime_error("Panel preview GPU readback failed");
        // Metal delivers readbacks on the owner thread after completion.
        for(int i=0;i<3&&!capture->done;++i){
            device_->beginFrame(Side,Side);device_->endFrame();
            if(!device_->waitForSubmittedGpuWork())break;
        }
        if(!capture->done||capture->pixels.empty())throw std::runtime_error("Panel preview capture missing");
        return std::move(capture->pixels);
    }
private:
    void shutdown() noexcept {
        if(!device_)return;
        (void)device_->waitForResourceIdle();
        if(color_.valid())device_->destroyTexture(color_);
        if(depth_.valid())device_->destroyTexture(depth_);
        renderer_.shutdown();if(lease_)lease_->retire();
        device_.reset();
    }
protected:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> lease_;
    render::Renderer3D renderer_;
    gfx::TextureHandle color_,depth_;
};
}
