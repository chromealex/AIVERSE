// Bounded scene presentation for authored volumetric particle graphs.
// Source identity comes from ECS/MapDoc; GPU state never enters simulation.
#pragma once
#include "SpectraGraphRuntime.h"
#include "engine/render/Prisma.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct SceneVolumetricParticleSource {
    std::string identity, effect;
    uint32_t revision=0;
    render::float3 position{}, direction{0,1,0};
    const Json* graph=nullptr; // borrowed for synchronize() only
    std::map<std::string,float> parameters;
    float scale=1.f;
    bool depthAware=false;
};

// Render into a separate HDR target: the opaque depth is sampled only after
// the geometry pass has closed. Shared by game, scene, and preview hosts.
class VolumetricParticleDepthTarget final {
public:
    void clear(){retireTarget();error_.clear();}
    const std::string& error()const{return error_;}
    template<class Draw>
    gfx::TextureHandle compose(gfx::Device& device,const render::PostFrame& frame,
                               gfx::TextureHandle color,gfx::TextureHandle depth,Draw&& draw){
        gfx::TextureInfo info{};
        if(!color.valid()||!depth.valid()||!device.textureInfo(color,info)||
           info.format!=gfx::PixelFormat::RGBA16F||info.width!=frame.width||info.height!=frame.height||
           !ensureTarget(device,frame.width,frame.height)||!device.copyColor(color,target_)){
            error_="Volumetric particle HDR scene copy unavailable";return {};
        }
        gfx::RenderTargetPassDesc pass;pass.colorCount=1;
        pass.colors[0].texture=target_;pass.colors[0].clear=false;
        if(!device.beginRenderTargets(pass)){
            error_="Volumetric particle HDR target pass unavailable";return {};
        }
        device.setViewport({0,0,frame.width,frame.height});
        const bool ok=draw(depth);
        device.endPass();
        lastFence_=std::max(lastFence_,device.gpuSubmissionFence());
        if(!ok){error_="Volumetric particle depth draw failed";return {};}
        error_.clear();return target_;
    }
private:
    bool ensureTarget(gfx::Device& device,int width,int height){
        if(target_.valid()&&width==width_&&height==height_)return true;
        if(!budget_)budget_=device.acquireGpuAllocationBudget();
        if(!budget_)return false;
        budget_->collect(device.gpuCompletedFence());
        const size_t bytes=size_t(width)*size_t(height)*8;
        if(!budget_->reserve(bytes))return false;
        gfx::TextureDesc desc;desc.width=width;desc.height=height;
        desc.format=gfx::PixelFormat::RGBA16F;desc.renderTarget=true;
        desc.label="SpectraFX volumetric particles HDR";
        const auto next=device.createTexture(desc);
        if(!next.valid()){budget_->retire(bytes,0);return false;}
        retireTarget();device_=&device;target_=next;charge_=bytes;width_=width;height_=height;
        return true;
    }
    void retireTarget(){
        if(target_.valid()&&device_)device_->destroyTexture(target_);
        if(budget_&&charge_){budget_->retire(charge_,lastFence_);if(device_)budget_->collect(device_->gpuCompletedFence());}
        target_={};charge_=0;lastFence_=0;width_=height_=0;
    }
    gfx::Device* device_=nullptr;
    std::shared_ptr<gfx::GpuAllocationBudget> budget_;
    gfx::TextureHandle target_{};
    size_t charge_=0;
    uint64_t lastFence_=0;
    int width_=0,height_=0;
    std::string error_;
};

class SceneVolumetricParticleWorld final {
public:
    static constexpr size_t capacity=16;
    void clear(){entries_.clear();depthTarget_.clear();error_.clear();}
    bool active()const{return !entries_.empty();}
    bool hasDepth()const{
        for(const auto& [id,e]:entries_)if(e.depthAware)return true;
        return false;
    }
    const std::string& error()const{return error_;}
    void synchronize(gfx::Device& device,const std::vector<SceneVolumetricParticleSource>& sources,
                     const fx::SdfResourceReader& reader){
        error_.clear();
        std::map<std::string,const SceneVolumetricParticleSource*> desired;
        for(const auto& source:sources)if(!source.identity.empty()&&source.graph)
            desired.emplace(source.identity,&source);
        for(auto it=entries_.begin();it!=entries_.end();){
            const auto found=desired.find(it->first);
            // Like authored scene gas, an admitted source keeps its original
            // anchor until the component revision changes. Ordinary pose
            // interpolation must not rebuild GPU resources every frame.
            if(found==desired.end()||found->second->revision!=it->second.revision||
               found->second->effect!=it->second.effect)
                it=entries_.erase(it);
            else ++it;
        }
        size_t admitted=0;
        for(const auto& [id,source]:desired){
            if(entries_.count(id))continue;
            if(admitted++>=capacity||entries_.size()>=capacity){error_="Volumetric particle scene capacity reached";break;}
            if(source->scale!=1.f){error_="Volumetric particles require unit source scale";continue;}
            auto runtime=std::make_unique<GraphRuntime>();
            GraphRuntimeOptions options;options.preference=ComputePreference::RequireGpu;
            options.gpuProgram=true;options.proceduralBillboards=true;
            options.sdfReader=reader;
            if(!runtime->initGraph(&device,*source->graph,Json::object(),options)||
               !runtime->gpuActive()||!runtime->dataInterfaceBindings().empty()){
                error_=runtime->reason().empty()?"Volumetric particle graph needs a compatible GPU program without live data interfaces":runtime->reason();
                continue;
            }
            bool valid=true;
            for(const auto& [name,value]:source->parameters)
                if(!runtime->setNamedProgramParameter(name,value,false)){valid=false;break;}
            if(!valid||!runtime->burstAt(source->position,source->direction)){
                error_=runtime->reason().empty()?"Volumetric particle source rejected":runtime->reason();
                continue;
            }
            entries_.emplace(id,Entry{source->effect,source->revision,source->position,
                                      source->depthAware,std::move(runtime)});
        }
    }
    void prepare(const fx::SpectraFxView& view,float dt){
        if(!view.valid()||!std::isfinite(dt)||dt<0)return;
        for(auto& [id,e]:entries_){
            e.runtime->setView(view);
            // ComputeParticles::step prepares the optional GPU depth sort.
            if(!e.runtime->step(dt))
                error_=e.runtime->reason();
        }
    }
    void drawInline(const fx::FxFog& fog={},render::Renderer3D* renderer=nullptr){
        for(auto& [id,e]:entries_)if(!e.depthAware)e.runtime->draw(fog,renderer);
    }
    gfx::TextureHandle composeDepth(gfx::Device& device,const render::PostFrame& frame,
                                    gfx::TextureHandle color,gfx::TextureHandle depth){
        if(!hasDepth())return {};
        auto out=depthTarget_.compose(device,frame,color,depth,[&](gfx::TextureHandle sampledDepth){
            bool ok=true;
            for(auto& [id,e]:entries_)if(e.depthAware&&!e.runtime->drawWithVolumeDepth(sampledDepth)){
                error_=e.runtime->reason();ok=false;
            }
            return ok;
        });
        if(!out.valid()&&error_.empty())error_=depthTarget_.error();
        return out;
    }
private:
    struct Entry{
        std::string effect;uint32_t revision;render::float3 position;
        bool depthAware;std::unique_ptr<GraphRuntime> runtime;
    };
    std::map<std::string,Entry> entries_;
    VolumetricParticleDepthTarget depthTarget_;
    std::string error_;
};
} // namespace aiverse::spectra
