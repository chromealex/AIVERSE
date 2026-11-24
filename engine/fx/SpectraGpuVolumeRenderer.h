// Native SpectraFX HDR world-volume contribution. Render owner only.
// Device outlives renderer and grid. No framebuffer postprocess ownership.
#pragma once
#include "SpectraGpuVolumeGrid.h"
#include "SpectraVolumeRenderShaders.h"
#include "engine/render/Renderer3D.h"
#include <array>

namespace aiverse::spectra::volume {
struct PointLight {
    float3 position{},color{1,1,1};
    float radius=5,intensity=1;
    bool valid()const{
        return bounded(position,1e6f)&&bounded(color,64)&&color.x>=0&&color.y>=0&&color.z>=0&&
            between(radius,.01f,1000)&&between(intensity,0,100);
    }
};
struct RenderSettings {
    uint32_t steps=64,shadowSteps=4;
    float maxDistance=1000,ambient=.4f,shadowDistance=30,anisotropy=.2f;
    std::vector<PointLight> pointLights;
    bool valid()const{
        if(!steps||steps>128||shadowSteps>8||pointLights.size()>4||
           !between(maxDistance,.01f,1e5f)||!between(ambient,0,10)||
           !between(shadowDistance,0,1000)||!between(anisotropy,-.9f,.9f))return false;
        for(const auto& light:pointLights)if(!light.valid())return false;
        return true;
    }
};
struct VolumeRenderUniforms {
    render::mat4 invVP{};
    std::array<uint32_t,4> meta{};
    render::float4 domain{},camera{},smoke{},fire{},controls{},sun{},sunColor{},limits{};
    std::array<render::float4,4> pointPos{},pointColor{};
};
static_assert(sizeof(VolumeRenderUniforms)==336);
inline bool supportsVolumeRender(const gfx::Device& d){
    return supportsGpuVolume(d)&&d.supportsStorageTexture(gfx::PixelFormat::RGBA16F,gfx::TextureDimension::Texture2D);
}
class GpuVolumeRenderer final {
public:
    GpuVolumeRenderer()=default;
    GpuVolumeRenderer(const GpuVolumeRenderer&)=delete;
    GpuVolumeRenderer& operator=(const GpuVolumeRenderer&)=delete;
    ~GpuVolumeRenderer(){shutdown();}
    const std::string& error()const{return error_;}
    size_t allocatedBytes()const{return charge_;}
    uint64_t dispatches()const{return dispatches_;}
    void shutdown(){
        if(device_){
            retireTarget();
            if(compute_.valid())device_->destroyCompute(compute_);
        }
        compute_={};device_=nullptr;budget_.reset();dispatches_=0;
    }
    // Called after opaque scene pass ends, before tone mapping. Return a
    // borrowed HDR texture valid until resize/shutdown; no readback. The
    // caller must consume it on this Device in the same frame, ordered
    // before the next write. A rejected render returns an invalid handle.
    gfx::TextureHandle draw(gfx::Device& d,GpuGasGrid& grid,const render::PostFrame& f,
            gfx::TextureHandle scene,gfx::TextureHandle depth,const Material& material,
            const RenderSettings& settings={}){
        error_.clear();
        if(!supportsVolumeRender(d)||!d.gpuHealthy()||!d.canUpdateSharedBuffers()||!d.gpuSubmissionFence()||
           (device_&&device_!=&d)||!material.valid()||!settings.valid()||f.width<=0||f.height<=0||
           uint64_t(f.width)*uint64_t(f.height)>4194304ull||f.width>4096||f.height>4096)
            return fail("Unsupported or invalid HDR volume render request");
        gfx::TextureInfo sc{},dp{};
        if(!scene.valid()||!depth.valid()||scene==target_||scene==depth||
           !d.textureInfo(scene,sc)||!d.textureInfo(depth,dp)||
           sc.format!=gfx::PixelFormat::RGBA16F||dp.format!=gfx::PixelFormat::Depth||
           sc.width!=f.width||sc.height!=f.height||dp.width!=f.width||dp.height!=f.height)
            return fail("Volume render requires matching separate HDR scene and depth textures");
        auto view=grid.view();
        if(!view.valid()||view.device!=&d)return fail("No published GPU volume on this Device");
        VolumeRenderUniforms u;
        u.invVP=f.viewProj.inverse();
        const auto product=f.viewProj*u.invVP;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x){
            const float v=product.at(y,x);
            if(!std::isfinite(v)||std::abs(v-(x==y?1.f:0.f))>.02f)return fail("Invalid volume view projection");
        }
        if(!bounded(f.lighting.sunDir,1e6f)||!bounded(f.lighting.sunColor,64)||
           !between(f.lighting.sunIntensity,0,100))return fail("Invalid scene sun");
        u.meta={uint32_t(f.width),uint32_t(f.height),view.dimension,settings.steps};
        u.domain={view.origin.x,view.origin.y,view.origin.z,view.cellSize};
        u.camera={d.info().renderTargetFlipY?1.f:0.f,d.info().clipDepthZeroToOne?1.f:2.f,settings.maxDistance,0};
        u.smoke={material.smokeColor.x,material.smokeColor.y,material.smokeColor.z,material.extinction};
        u.fire={material.fireColor.x,material.fireColor.y,material.fireColor.z,material.emission};
        u.controls={material.emissionThreshold,settings.ambient,settings.shadowDistance,settings.anisotropy};
        const auto sun=render::normalize(f.lighting.sunDir);
        u.sun={sun.x,sun.y,sun.z,f.lighting.sunIntensity};
        u.sunColor={std::max(0.f,f.lighting.sunColor.x),std::max(0.f,f.lighting.sunColor.y),std::max(0.f,f.lighting.sunColor.z),0};
        u.limits={float(settings.shadowSteps),float(settings.pointLights.size()),0,0};
        for(size_t i=0;i<settings.pointLights.size();++i){
            const auto& l=settings.pointLights[i];
            u.pointPos[i]={l.position.x,l.position.y,l.position.z,l.radius};
            u.pointColor[i]={l.color.x,l.color.y,l.color.z,l.intensity};
        }
        device_=&d;
        if(!budget_)budget_=d.acquireGpuAllocationBudget();
        if(!budget_)return fail("Volume render ledger unavailable");
        if(!compute_.valid())compute_=d.createCompute({render_shaders::kMetal,render_shaders::kGLSL,"SpectraFX world volume HDR"});
        if(!compute_.valid()||!ensureTarget(f.width,f.height))return fail("Volume render resource admission failed");
        if(!d.beginCompute(compute_))return fail("Volume render compute begin failed");
        lastFence_=std::max(lastFence_,d.gpuSubmissionFence());
        d.setComputeBuffer(0,view.cells);d.setComputeBuffer(1,view.solid);
        const bool bound=d.bindComputeStorageTexture(0,target_);
        d.setComputeTexture(1,scene);d.setComputeTexture(2,depth);
        d.setComputeUniforms(&u,sizeof(u));
        const bool ok=bound&&d.dispatchChecked(uint32_t(f.width),uint32_t(f.height));
        d.endCompute();
        if(!ok)return fail("Volume HDR dispatch rejected");
        ++dispatches_;return target_;
    }
private:
    gfx::TextureHandle fail(const char* error){error_=error;return {};}
    bool ensureTarget(int w,int h){
        if(target_.valid()&&width_==w&&height_==h)return true;
        budget_->collect(device_->gpuCompletedFence());
        const size_t bytes=size_t(w)*h*8;
        if(!budget_->reserve(bytes))return false;
        gfx::TextureDesc desc;desc.width=w;desc.height=h;desc.format=gfx::PixelFormat::RGBA16F;
        desc.storage=true;desc.filter=gfx::Filter::Nearest;desc.label="SpectraFX volume HDR contribution";
        const auto next=device_->createTexture(desc);
        if(!next.valid()){budget_->retire(bytes,0);return false;}
        retireTarget();
        target_=next;charge_=bytes;width_=w;height_=h;return true;
    }
    void retireTarget(){
        if(target_.valid())device_->destroyTexture(target_);
        if(budget_&&charge_){budget_->retire(charge_,lastFence_);budget_->collect(device_->gpuCompletedFence());}
        target_={};charge_=0;lastFence_=0;width_=height_=0;
    }
    gfx::Device* device_=nullptr;
    std::shared_ptr<gfx::GpuAllocationBudget> budget_;
    gfx::ComputeHandle compute_{};
    gfx::TextureHandle target_{};
    int width_=0,height_=0;
    size_t charge_=0;
    uint64_t lastFence_=0,dispatches_=0;
    std::string error_;
};
}
