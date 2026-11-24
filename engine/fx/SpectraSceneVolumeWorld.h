// Shared presentation owner for dense volume instances. No ECS or game dependency.
#pragma once
#include "SpectraGpuVolumeRenderer.h"
#include "SpectraVolumeGraph.h"
#include "SpectraGraphRuntime.h"
#include <map>
#include <set>
#include <memory>
#include <string>

namespace aiverse::spectra::volume {
class VolumeInstance final {
public:
    bool init(gfx::Device& device,const Asset& asset,float3 anchor={},const fx::SdfResourceReader& reader={}){
        if(!supportsVolumeRender(device)||!bounded(anchor,1e6f)||!between(asset.physisWindResponse,0,20)||!between(asset.physisGravityBlend,0,1)){error_="Invalid volume anchor or GPU backend";return false;}
        Asset placed=asset;placed.simulation.origin=placed.simulation.origin+anchor;
        for(auto& s:placed.sources)s.position=s.position+anchor;
        if(placed.impulse)placed.impulse->position=placed.impulse->position+anchor;
        fx::StaticSdfVolume sdf;
        if(!placed.obstacle.empty()){
            if(!reader){error_="Volume obstacle requires a resource reader";return false;}
            if(!fx::loadSdfBinary(placed.obstacle,reader,sdf,error_))return false;
            sdf.origin=sdf.origin+anchor;
        }
        if(!placed.simulation.valid()||!placed.material.valid()||placed.sources.size()>kMaxSources||
           (placed.impulse&&(!placed.impulse->valid()||!between(placed.impulseSeconds,.001f,1.f)))){
            error_="Invalid placed volume";return false;
        }
        for(const auto& s:placed.sources)if(!s.valid()){error_="Invalid placed source";return false;}
        auto next=std::make_unique<GpuGasGrid>();
        if(!next->init(device,placed.simulation,sdf)||!next->setSources(placed.sources)){
            error_=next->error();return false;
        }
        if(placed.impulse&&!next->queueImpulse(*placed.impulse,placed.impulseSeconds)){
            error_="Volume impulse rejected";return false;
        }
        renderer_.shutdown();grid_=std::move(next);asset_=std::move(placed);anchor_=anchor;deposits_.clear();error_.clear();return true;
    }
    void shutdown(){renderer_.shutdown();grid_.reset();asset_={};deposits_.clear();error_.clear();}
    bool step(){
        if(!grid_){error_="No volume";return false;}
        auto sources=asset_.sources;
        sources.insert(sources.end(),deposits_.begin(),deposits_.end());
        if(!grid_->setSources(sources)||!grid_->step()){error_=grid_->error();return false;}
        deposits_.clear();return true;
    }
    bool clear(bool retrigger){
        if(!grid_||!grid_->clear()){error_=grid_?grid_->error():"No volume";return false;}
        deposits_.clear();
        return !retrigger||!asset_.impulse||trigger();
    }
    bool trigger(){return grid_&&asset_.impulse&&(grid_->impulsePending()||grid_->queueImpulse(*asset_.impulse,asset_.impulseSeconds));}
    // Render-owner API: update authored continuous sources with transient particle
    // deposits for one accepted tick. A failed step retains deposits for retry.
    bool deposit(const Source& source){
        if(!grid_||!source.valid()||asset_.sources.size()+deposits_.size()>=kMaxSources)return false;
        deposits_.push_back(source);return true;
    }
    // PHYSIS wind channels are velocity, not acceleration. Convert to an
    // authored acceleration approximation using a response (1/s).
    // Missing or invalid vectors restore authored values, never stale fields.
    void applyEnvironment(const DataInterfaceRegistry* registry){
        if(!grid_)return;
        auto wind=asset_.simulation.wind,gravity=asset_.simulation.gravity;
        const auto at=center();
        const auto sample=[&](const char* x,const char* y,const char* z,float3& v){
            return registry&&registry->sample("physis",x,at,v.x)&&
                registry->sample("physis",y,at,v.y)&&registry->sample("physis",z,at,v.z)&&bounded(v,1e4f);
        };
        float3 v{};
        if(asset_.physisWindResponse>0&&sample("wind_x","wind_y","wind_z",v)){
            wind=wind+v*asset_.physisWindResponse;
            wind={std::clamp(wind.x,-100.f,100.f),std::clamp(wind.y,-100.f,100.f),std::clamp(wind.z,-100.f,100.f)};
        }
        if(asset_.physisGravityBlend>0&&sample("gravity_x","gravity_y","gravity_z",v)&&bounded(v,100))
            gravity=gravity+(v-gravity)*asset_.physisGravityBlend;
        grid_->setWind(wind);grid_->setGravity(gravity);
    }
    bool hasImpulse()const{return bool(asset_.impulse);}
    bool published()const{return grid_&&grid_->published();}
    const std::string& error()const{return error_;}
    float3 center()const{
        const auto& c=asset_.simulation;const float half=c.cellSize*float(c.dimension)*.5f;
        return c.origin+float3{half,half,half};
    }
    size_t bytes()const{return (grid_?grid_->stats().allocatedBytes:0)+renderer_.allocatedBytes();}
    GpuGasGrid* grid(){return grid_.get();}
    gfx::TextureHandle draw(gfx::Device& d,const render::PostFrame& f,gfx::TextureHandle color,gfx::TextureHandle depth){
        if(!published())return {};
        auto out=renderer_.draw(d,*grid_,f,color,depth,asset_.material,settings_);
        if(!out.valid())error_=renderer_.error();
        return out;
    }
    bool setRenderSettings(const RenderSettings& settings){if(!settings.valid())return false;settings_=settings;return true;}
private:
    std::unique_ptr<GpuGasGrid> grid_;
    GpuVolumeRenderer renderer_;
    Asset asset_;
    std::vector<Source> deposits_;
    RenderSettings settings_;
    float3 anchor_{};
    std::string error_;
};
struct SceneHybridDefinition {
    Json graph;
    ParticleVolumeCoupling coupling;
};
struct SceneVolumeSource {
    std::string identity,effect;
    uint32_t revision=0;
    float3 anchor{};
    const Asset* asset=nullptr; // borrowed only during synchronize
    const SceneHybridDefinition* hybrid=nullptr;
    float3 direction{0,1,0};
    std::map<std::string,float> parameters;
};
// Fixed-anchor, bounded scene presentation pool. Source revision changes
// recreate volume; ordinary moving poses do NOT reset the gas every frame.
class SceneVolumeWorld final {
public:
    static constexpr size_t capacity=4;
    void clear(){entries_.clear();error_.clear();}
    bool active()const{return !entries_.empty();}
    bool renderable()const{for(const auto& [id,e]:entries_)if(e.instance->published())return true;return false;}
    const std::string& error()const{return error_;}
    size_t size()const{return entries_.size();}
    size_t bytes()const{size_t n=0;for(const auto& [id,e]:entries_)n+=e.instance->bytes();return n;}
    void synchronize(gfx::Device& d,const std::vector<SceneVolumeSource>& rows,const fx::SdfResourceReader& reader){
        error_.clear();std::map<std::string,const SceneVolumeSource*> desired;
        for(const auto& row:rows)if(!row.identity.empty()&&row.asset&&bounded(row.anchor,1e6f))desired.emplace(row.identity,&row);
        for(auto it=entries_.begin();it!=entries_.end();){
            auto found=desired.find(it->first);
            if(found==desired.end()||found->second->revision!=it->second.revision||
               found->second->effect!=it->second.effect)it=entries_.erase(it);else ++it;
        }
        size_t attempts=0;
        for(const auto& [id,row]:desired){
            if(entries_.count(id))continue;
            if(attempts++>=capacity){error_="Scene volume admission work limit";break;}
            if(entries_.size()>=capacity){error_="Scene volume capacity reached";break;}
            auto instance=std::make_unique<VolumeInstance>();
            if(!instance->init(d,*row->asset,row->anchor,reader)){error_=instance->error();continue;}
            std::unique_ptr<GraphRuntime> particles;
            ParticleVolumeCoupling coupling;
            if(row->hybrid){
                coupling=row->hybrid->coupling;
                GraphRuntimeOptions options;options.preference=ComputePreference::RequireGpu;
                options.gpuProgram=true;options.sdfReader=reader;
                particles=std::make_unique<GraphRuntime>();
                if(!particles->initGraph(&d,row->hybrid->graph,Json::object(),options)||
                   !particles->dataInterfaceBindings().empty()){
                    error_="Scene hybrid requires a compatible dedicated GPU graph without live data interfaces";continue;
                }
                bool valid=true;
                for(const auto& [name,value]:row->parameters)
                    if(!particles->setNamedProgramParameter(name,value,false)){valid=false;break;}
                if(!valid||!particles->burstAt(row->anchor,row->direction)){
                    error_="Scene hybrid initial burst rejected";continue;
                }
            }
            entries_.emplace(id,Entry{row->effect,row->revision,0,std::move(instance),std::move(particles),coupling,false});
        }
    }
    // Explicit world-space particle deposit into a named resident volume.
    bool deposit(const std::string& identity,const Source& source){
        auto it=entries_.find(identity);
        return it!=entries_.end()&&it->second.instance->deposit(source);
    }
    // Borrowed GPU view for ordered particle compute; never read back here.
    GpuVolumeView view(const std::string& identity){
        auto it=entries_.find(identity);
        if(it==entries_.end())return {};
        auto* grid=it->second.instance->grid();
        return grid?grid->view():GpuVolumeView{};
    }
    void applyEnvironment(const DataInterfaceRegistry* registry){
        for(auto& [id,e]:entries_)e.instance->applyEnvironment(registry);
    }
    void setView(const fx::SpectraFxView& view){
        for(auto& [id,e]:entries_)if(e.particles)e.particles->setView(view);
    }
    void drawParticles(const fx::FxFog& fog={}){
        for(auto& [id,e]:entries_)if(e.particles&&!e.failed)e.particles->draw(fog);
    }
    void step(float dt){
        if(!std::isfinite(dt)||dt<0)return;
        for(auto& [id,e]:entries_){
            if(e.failed)continue;
            e.phase=std::min(4.f*kTick,e.phase+std::min(dt,.1f));
            // Initialize a paused instance with an empty field; do not advance time.
            if(!e.instance->published()&&e.phase<kTick)continue;
            for(unsigned ticks=0;ticks<4&&e.phase>=kTick;++ticks){
                if(!e.instance->step()){error_=e.instance->error();break;}
                if(e.particles&&!e.particles->stepWithVolume(kTick,*e.instance->grid(),e.coupling.drag,e.coupling.deposit)){
                    error_=e.particles->reason();e.failed=true;break;
                }
                e.phase-=kTick;
            }
        }
    }
    gfx::TextureHandle compose(gfx::Device& d,const render::PostFrame& f,gfx::TextureHandle color,gfx::TextureHandle depth){
        std::array<VolumeInstance*,capacity> order{};size_t n=0;
        for(auto& [id,e]:entries_)if(e.instance->published())order[n++]=e.instance.get();
        const auto distance=[&](VolumeInstance* v){auto p=v->center()-f.cameraPos;return render::dot(p,p);};
        // Stable tie order from identity; sequential approximate composition.
        std::stable_sort(order.begin(),order.begin()+n,[&](auto* a,auto* b){return distance(a)>distance(b);});
        auto result=color;
        for(size_t i=0;i<n;++i){
            auto next=order[i]->draw(d,f,result,depth);
            if(next.valid())result=next;else error_=order[i]->error();
        }
        return result==color?gfx::TextureHandle{}:result;
    }
private:
    struct Entry{std::string effect;uint32_t revision;float phase;std::unique_ptr<VolumeInstance> instance;std::unique_ptr<GraphRuntime> particles;ParticleVolumeCoupling coupling;bool failed;};
    std::map<std::string,Entry> entries_;
    std::string error_;
};
}
