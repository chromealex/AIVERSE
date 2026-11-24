// Dense GPU gas simulation. Render-owner thread; Device outlives this grid.
// No gameplay state, no implicit CPU fallback, no normal-frame readback.
#pragma once
#include "SpectraVolumeAsset.h"
#include "SpectraVolumeGpuShaders.h"
#include "engine/gfx/Gfx.h"
#include <array>
#include <memory>
#include <utility>

namespace aiverse::spectra::volume {
struct GpuCell {render::float4 velocity{},fields{};}; // density, temperature, fuel, reserved
static_assert(sizeof(GpuCell)==32);
struct GpuSourceData {render::float4 positionRadius{},rates{},acceleration{};};
static_assert(sizeof(GpuSourceData)==48);
struct GpuVolumeUniforms {
    std::array<uint32_t,4> meta{};
    render::float4 originCell{},decay{},reaction{},buoyancy{},wind{},up{};
    std::array<GpuSourceData,kMaxSources+1> sources{};
};
static_assert(sizeof(GpuVolumeUniforms)==928);
struct GpuVolumeView {
    gfx::Device* device=nullptr;
    gfx::BufferHandle cells{},solid{};
    float3 origin{};
    float cellSize=0;
    uint32_t dimension=0;
    uint64_t tick=0;
    bool valid()const{return device&&cells.valid()&&solid.valid()&&dimension>=kMinDimension;}
};
struct GpuVolumeStats {
    uint64_t ticks=0,dispatches=0,rejectedTicks=0;
    size_t allocatedBytes=0,maskUploadBytes=0;
};
inline size_t gpuVolumeBytes(uint32_t dimension){
    if(dimension<kMinDimension||dimension>kMaxDimension)return 0;
    return size_t(dimension)*dimension*dimension*(3*sizeof(GpuCell)+sizeof(uint32_t)+3*sizeof(float));
}
inline bool supportsGpuVolume(const gfx::Device& device){
    return device.supportsCompute()&&(device.info().backend==gfx::Backend::Metal||
        (device.info().backend==gfx::Backend::Vulkan&&device.supportsStorageUniformCompute(9)));
}
class GpuGasGrid final {
public:
    GpuGasGrid()=default;
    GpuGasGrid(const GpuGasGrid&)=delete;
    GpuGasGrid& operator=(const GpuGasGrid&)=delete;
    ~GpuGasGrid(){shutdown();}
    bool init(gfx::Device& device,const Config& config,const fx::StaticSdfVolume& obstacle={},
              std::shared_ptr<gfx::GpuAllocationBudget> budget={}){
        if(!config.valid()||!obstacle.valid()||!supportsGpuVolume(device)||!device.gpuHealthy()||
           !device.canUpdateSharedBuffers()){error_="Invalid or unsupported GPU volume configuration";return false;}
        // Candidate allocation leaves previous valid grid intact on failure.
        GpuGasGrid next;next.device_=&device;next.cfg_=config;
        next.budget_=device.acquireGpuAllocationBudget(std::move(budget));
        if(!next.budget_){error_="GPU volume ledger mismatch";return false;}
        const size_t charge=gpuVolumeBytes(config.dimension);
        if(!next.budget_->reserve(charge)){error_="GPU volume allocation budget exhausted";return false;}
        next.stats_.allocatedBytes=charge;
        const uint32_t n=config.dimension;next.count_=n*n*n;
        next.mask_.resize(next.count_);
        for(uint32_t z=0;z<n;++z)for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){
            const auto p=config.origin+float3{float(x)+.5f,float(y)+.5f,float(z)+.5f}*config.cellSize;
            float distance=0;float3 gradient{};
            next.mask_[(z*n+y)*n+x]=obstacle.dimension&&obstacle.sample(p,distance,gradient)&&distance<=0?1u:0u;
        }
        auto allocate=[&](size_t size,const char* label){
            gfx::BufferDesc d;d.type=gfx::BufferType::Storage;d.usage=gfx::Usage::Immutable;
            d.size=size;d.label=label;return device.createBuffer(d);
        };
        for(auto& h:next.cells_)h=allocate(size_t(next.count_)*sizeof(GpuCell),"SpectraFX volume cells");
        next.solid_=allocate(size_t(next.count_)*sizeof(uint32_t),"SpectraFX volume solid mask");
        for(auto& h:next.pressure_)h=allocate(size_t(next.count_)*sizeof(float),"SpectraFX volume pressure");
        next.divergence_=allocate(size_t(next.count_)*sizeof(float),"SpectraFX volume divergence");
        next.compute_=device.createCompute({shaders::kMetal,shaders::kGLSL,"SpectraFX dense volume gas",
            gfx::VulkanComputeLayout::StorageUniforms,9});
        if(!next.compute_.valid()||!next.solid_.valid()||!next.divergence_.valid()||
           !next.cells_[0].valid()||!next.cells_[1].valid()||!next.cells_[2].valid()||
           !next.pressure_[0].valid()||!next.pressure_[1].valid()){
            error_="GPU volume resources: "+device.lastError();return false;
        }
        swap(next);error_.clear();return true;
    }
    bool initAsset(gfx::Device& device,const Asset& asset,const fx::SdfResourceReader& reader={},
                   std::shared_ptr<gfx::GpuAllocationBudget> budget={}){
        error_.clear();
        if(!asset.simulation.valid()||!asset.material.valid()||asset.sources.size()>kMaxSources){
            error_="Invalid volume asset";return false;
        }
        for(const auto& source:asset.sources)if(!source.valid()){error_="Invalid volume source";return false;}
        if(asset.impulse&&(!asset.impulse->valid()||!between(asset.impulseSeconds,.001f,1.f))){error_="Invalid volume impulse";return false;}
        fx::StaticSdfVolume sdf;
        if(!asset.obstacle.empty()&&(!fx::validSdfResourcePath(asset.obstacle)||!reader||
            !fx::loadSdfBinary(asset.obstacle,reader,sdf,error_))){
            if(error_.empty())error_="Invalid volume SDF reader or resource";return false;
        }
        if(!init(device,asset.simulation,sdf,std::move(budget)))return false;
        if(!setSources(asset.sources))return false;
        return !asset.impulse||queueImpulse(*asset.impulse,asset.impulseSeconds);
    }
    void shutdown(){
        if(device_){
            for(auto h:cells_)if(h.valid())device_->destroyBuffer(h);
            for(auto h:pressure_)if(h.valid())device_->destroyBuffer(h);
            if(solid_.valid())device_->destroyBuffer(solid_);
            if(divergence_.valid())device_->destroyBuffer(divergence_);
            if(compute_.valid())device_->destroyCompute(compute_);
            if(budget_&&stats_.allocatedBytes){
                budget_->retire(stats_.allocatedBytes,lastFence_);budget_->collect(device_->gpuCompletedFence());
            }
        }
        device_=nullptr;budget_.reset();cells_={};pressure_={};solid_={};divergence_={};compute_={};
        count_=0;current_=0;initialized_=false;maskUploaded_=false;impulsePending_=false;
        lastFence_=0;stats_={};mask_.clear();sources_.clear();
    }
    bool ready()const{return device_&&compute_.valid();}
    bool published()const{return ready()&&initialized_&&device_->gpuHealthy();}
    const Config& config()const{return cfg_;}
    const GpuVolumeStats& stats()const{return stats_;}
    const std::string& error()const{return error_;}
    bool setSources(const std::vector<Source>& sources){
        if(!ready()||sources.size()>kMaxSources)return false;
        for(const auto& s:sources)if(!s.valid())return false;
        sources_=sources;return true;
    }
    bool setWind(float3 v){if(!ready()||!bounded(v,100))return false;cfg_.wind=v;return true;}
    bool setGravity(float3 v){if(!ready()||!bounded(v,100))return false;cfg_.gravity=v;return true;}
    // One queued impulse applied after transport/reaction on next accepted tick.
    // Failed encoding preserves the queue; no unbounded host accumulation.
    bool impulsePending()const{return impulsePending_;}
    bool queueImpulse(const Source& source,float seconds=1.f){
        if(!ready()||impulsePending_||!source.valid()||!between(seconds,0,1))return false;
        impulse_=source;impulseSeconds_=seconds;impulsePending_=true;return true;
    }
    bool clear(){
        if(!canEncode()||!uploadMask())return fail("GPU volume clear requires active healthy frame");
        const uint32_t candidate=(current_+1)%3;
        auto u=uniforms();
        if(!dispatch(0,cells_[current_],cells_[candidate],pressure_[0],pressure_[1],u))
            return fail("GPU volume clear dispatch rejected");
        current_=candidate;initialized_=true;stats_.ticks=0;impulsePending_=false;error_.clear();return true;
    }
    bool step(){
        if(!canEncode()||!uploadMask())return reject("GPU volume requires active healthy frame and mask upload");
        if(!initialized_){
            auto u=uniforms();
            if(!dispatch(0,cells_[1],cells_[0],pressure_[0],pressure_[1],u))
                return reject("GPU volume initial clear rejected");
            current_=0;initialized_=true;
        }
        const uint32_t advected=(current_+1)%3,candidate=(current_+2)%3;
        auto u=uniforms();
        if(!dispatch(1,cells_[current_],cells_[advected],pressure_[0],pressure_[1],u))
            return reject("GPU volume advection rejected");
        uint32_t p=0;
        if(cfg_.pressureIterations){
            if(!dispatch(2,cells_[advected],cells_[candidate],pressure_[1],pressure_[0],u))
                return reject("GPU volume divergence rejected");
            for(uint32_t iteration=0;iteration<cfg_.pressureIterations;++iteration){
                if(!dispatch(3,cells_[advected],cells_[candidate],pressure_[p],pressure_[1-p],u))
                    return reject("GPU volume pressure iteration rejected");
                p=1-p;
            }
        }
        if(!dispatch(4,cells_[advected],cells_[candidate],pressure_[p],pressure_[1-p],u))
            return reject("GPU volume projection rejected");
        current_=candidate;++stats_.ticks;impulsePending_=false;error_.clear();return true;
    }
    // Borrowed view valid until next step/reinit/shutdown. Host obtains it
    // inside the frame which consumes it, then encodes ordered render work.
    GpuVolumeView view(){
        if(!published()||!device_->gpuSubmissionFence())return {};
        markUsed();return {device_,cells_[current_],solid_,cfg_.origin,cfg_.cellSize,cfg_.dimension,stats_.ticks};
    }
    // Explicit diagnostic between frames, never called by normal simulation.
    bool snapshot(std::vector<GpuCell>& output){
        if(!published()||device_->gpuSubmissionFence()||!device_->canUpdateSharedBuffers())return false;
        std::vector<GpuCell> next(count_);
        if(!device_->readBuffer(cells_[current_],next.data(),next.size()*sizeof(GpuCell)))return false;
        output.swap(next);return true;
    }
private:
    void swap(GpuGasGrid& other){
        using std::swap;
        swap(device_,other.device_);swap(budget_,other.budget_);swap(cfg_,other.cfg_);
        swap(cells_,other.cells_);swap(pressure_,other.pressure_);swap(solid_,other.solid_);
        swap(divergence_,other.divergence_);swap(compute_,other.compute_);swap(mask_,other.mask_);
        swap(sources_,other.sources_);swap(impulse_,other.impulse_);swap(impulseSeconds_,other.impulseSeconds_);
        swap(count_,other.count_);swap(current_,other.current_);swap(initialized_,other.initialized_);
        swap(maskUploaded_,other.maskUploaded_);swap(impulsePending_,other.impulsePending_);
        swap(lastFence_,other.lastFence_);swap(stats_,other.stats_);
    }
    bool fail(const char* why){error_=why;return false;}
    bool reject(const char* why){++stats_.rejectedTicks;return fail(why);}
    bool canEncode()const{return ready()&&device_->gpuHealthy()&&device_->canUpdateSharedBuffers()&&device_->gpuSubmissionFence();}
    void markUsed(){
        const auto fence=device_->gpuSubmissionFence();
        lastFence_=fence?std::max(lastFence_,fence):gfx::GpuAllocationBudget::unknownFence;
    }
    bool uploadMask(){
        if(maskUploaded_)return true;
        markUsed();
        if(!device_->updateStorageBufferChecked(solid_,mask_.data(),mask_.size()*sizeof(uint32_t)))return false;
        maskUploaded_=true;stats_.maskUploadBytes=mask_.size()*sizeof(uint32_t);return true;
    }
    GpuVolumeUniforms uniforms()const{
        GpuVolumeUniforms u;
        u.meta={0,cfg_.dimension,uint32_t(sources_.size())+uint32_t(impulsePending_),cfg_.pressureIterations?1u:0u};
        u.originCell={cfg_.origin.x,cfg_.origin.y,cfg_.origin.z,cfg_.cellSize};
        u.decay={std::exp(-cfg_.velocityDecay*kTick),std::exp(-cfg_.smokeDecay*kTick),
            std::exp(-cfg_.temperatureDecay*kTick),std::exp(-cfg_.fuelDecay*kTick)};
        u.reaction={cfg_.ignition,cfg_.burnRate,cfg_.heatYield,cfg_.smokeYield};
        u.buoyancy={cfg_.buoyancy,cfg_.smokeWeight,cfg_.maxVelocity,0};
        u.wind={cfg_.wind.x,cfg_.wind.y,cfg_.wind.z,0};
        const auto up=render::length(cfg_.gravity)>1e-6f?render::normalize(-cfg_.gravity):float3{};
        u.up={up.x,up.y,up.z,0};
        auto pack=[](const Source& s,float dt){return GpuSourceData{
            {s.position.x,s.position.y,s.position.z,s.radius},{s.smoke,s.temperature,s.fuel,s.radialAcceleration},
            {s.acceleration.x,s.acceleration.y,s.acceleration.z,dt}};};
        for(size_t i=0;i<sources_.size();++i)u.sources[i]=pack(sources_[i],kTick);
        if(impulsePending_)u.sources[sources_.size()]=pack(impulse_,impulseSeconds_);
        return u;
    }
    bool dispatch(uint32_t mode,gfx::BufferHandle input,gfx::BufferHandle output,
                  gfx::BufferHandle pressureIn,gfx::BufferHandle pressureOut,GpuVolumeUniforms& u){
        if(!device_->beginCompute(compute_))return false;
        markUsed();
        device_->setComputeBuffer(0,input);device_->setComputeBuffer(1,output);
        device_->setComputeBuffer(2,solid_);device_->setComputeBuffer(3,pressureIn);
        device_->setComputeBuffer(4,pressureOut);device_->setComputeBuffer(5,divergence_);
        u.meta[0]=mode;device_->setComputeUniforms(&u,sizeof(u));
        const bool ok=device_->dispatchChecked(count_);
        device_->endCompute();
        if(ok)++stats_.dispatches;
        return ok;
    }
    gfx::Device* device_=nullptr;
    std::shared_ptr<gfx::GpuAllocationBudget> budget_;
    Config cfg_{};
    std::array<gfx::BufferHandle,3> cells_{};
    std::array<gfx::BufferHandle,2> pressure_{};
    gfx::BufferHandle solid_{},divergence_{};
    gfx::ComputeHandle compute_{};
    std::vector<uint32_t> mask_;
    std::vector<Source> sources_;
    Source impulse_{};float impulseSeconds_=0;
    uint32_t count_=0,current_=0;
    bool initialized_=false,maskUploaded_=false,impulsePending_=false;
    uint64_t lastFence_=0;
    GpuVolumeStats stats_{};
    std::string error_;
};
}
