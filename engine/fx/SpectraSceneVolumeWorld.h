// Shared presentation owner for dense volume instances. No ECS or game dependency.
#pragma once
#include "SpectraGpuVolumeRenderer.h"
#include "SpectraVolumeGraph.h"
#include <map>
#include <set>
#include <memory>
#include <string>

namespace aiverse::spectra::volume {
class VolumeInstance final {
public:
    bool init(gfx::Device& device,const Asset& asset,float3 anchor={},const fx::SdfResourceReader& reader={}){
        if(!supportsVolumeRender(device)||!bounded(anchor,1e6f)){error_="Invalid volume anchor or GPU backend";return false;}
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
        renderer_.shutdown();grid_=std::move(next);asset_=std::move(placed);anchor_=anchor;error_.clear();return true;
    }
    bool step(){if(!grid_||!grid_->step()){error_=grid_?grid_->error():"No volume";return false;}return true;}
    bool clear(bool retrigger){
        if(!grid_||!grid_->clear()){error_=grid_?grid_->error():"No volume";return false;}
        return !retrigger||!asset_.impulse||trigger();
    }
    bool trigger(){return grid_&&asset_.impulse&&(grid_->impulsePending()||grid_->queueImpulse(*asset_.impulse,asset_.impulseSeconds));}
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
    RenderSettings settings_;
    float3 anchor_{};
    std::string error_;
};
struct SceneVolumeSource {
    std::string identity,effect;
    uint32_t revision=0;
    float3 anchor{};
    const Asset* asset=nullptr; // borrowed only during synchronize
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
            entries_.emplace(id,Entry{row->effect,row->revision,0,std::move(instance)});
        }
    }
    void step(float dt){
        if(!std::isfinite(dt)||dt<0)return;
        for(auto& [id,e]:entries_){
            e.phase=std::min(4.f*kTick,e.phase+std::min(dt,.1f));
            // Initialize a paused instance with an empty field; do not advance time.
            if(!e.instance->published()&&e.phase<kTick)continue;
            for(unsigned ticks=0;ticks<4&&e.phase>=kTick;++ticks){
                if(!e.instance->step()){error_=e.instance->error();break;}
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
    struct Entry{std::string effect;uint32_t revision;float phase;std::unique_ptr<VolumeInstance> instance;};
    std::map<std::string,Entry> entries_;
    std::string error_;
};
}
