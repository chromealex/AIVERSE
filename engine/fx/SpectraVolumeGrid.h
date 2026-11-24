// SpectraFX dense gas presentation reference. No gameplay authority or RHI.
#pragma once
#include "SpectraStaticSdfVolume.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace aiverse::spectra::volume {
using render::float3;
inline constexpr uint32_t kMaxDimension=96,kMinDimension=8,kMaxSources=16,kMaxPressureIterations=24;
inline constexpr float kTick=1.f/60.f,kMaxScalar=16.f;
inline bool finite(float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool bounded(float3 v,float limit){
    return finite(v)&&std::abs(v.x)<=limit&&std::abs(v.y)<=limit&&std::abs(v.z)<=limit;
}
inline bool between(float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
struct Config {
    uint32_t dimension=32,pressureIterations=12;
    float3 origin{-2,-2,-2};
    float cellSize=.125f;
    float3 gravity{0,-9.81f,0},wind{}; // wind acceleration, world units/s^2
    float buoyancy=1,smokeWeight=.1f,velocityDecay=.15f;
    float smokeDecay=.25f,temperatureDecay=.6f,fuelDecay=.05f;
    float ignition=.5f,burnRate=3,heatYield=2,smokeYield=.5f,maxVelocity=20;
    bool valid()const{
        const float extent=cellSize*float(dimension);
        return dimension>=kMinDimension&&dimension<=kMaxDimension&&pressureIterations<=kMaxPressureIterations&&
            between(cellSize,.01f,100.f)&&bounded(origin,1e6f)&&
            bounded(origin+float3{extent,extent,extent},1e6f)&&bounded(gravity,100)&&bounded(wind,100)&&
            between(buoyancy,0,20)&&between(smokeWeight,0,20)&&between(velocityDecay,0,20)&&
            between(smokeDecay,0,20)&&between(temperatureDecay,0,20)&&between(fuelDecay,0,20)&&
            between(ignition,0,kMaxScalar)&&between(burnRate,0,60)&&between(heatYield,0,16)&&
            between(smokeYield,0,16)&&between(maxVelocity,.01f,100);
    }
};
struct Source {
    float3 position{};
    float radius=.5f,smoke=1,temperature=1,fuel=0;
    float radialAcceleration=0;
    float3 acceleration{0,2,0}; // weighted rate; impulse uses explicit duration
    float flickerHz=0;
    Source atTick(uint64_t tick) const {
        if(flickerHz==0)return *this;
        Source sample=*this;
        const double phase=6.28318530718*double(tick)*kTick*flickerHz;
        const float pulse=float(.5+.32*std::sin(phase)+.18*std::sin(1.71*phase));
        sample.radius*=.65f+.55f*pulse;
        sample.temperature*=.25f+1.25f*pulse;
        sample.fuel*=.25f+1.25f*pulse;
        return sample;
    }
    bool valid()const{
        return between(flickerHz,0,12)&&bounded(position,1e6f)&&between(radius,.01f,1000)&&
            between(smoke,0,100)&&between(temperature,0,100)&&between(fuel,0,100)&&between(radialAcceleration,-100,100)&&bounded(acceleration,100);
    }
};
struct Cell {float3 velocity{};float density=0,temperature=0,fuel=0;};
struct Stats {uint64_t ticks=0;size_t cells=0,solidCells=0;double density=0,temperature=0,fuel=0;float peakSpeed=0;};
struct Material {
    float3 smokeColor{.45f,.48f,.52f},fireColor{4.f,1.2f,.2f};
    float extinction=2,emission=1,emissionThreshold=.5f;
    float smokeDetail=0,emberIntensity=0;
    float3 smokeDetailVelocity{}; // world units per simulation second
    bool valid()const{
        const auto positive=[](float3 v,float max){return bounded(v,max)&&v.x>=0&&v.y>=0&&v.z>=0;};
        return positive(smokeColor,16)&&positive(fireColor,64)&&between(extinction,0,100)&&
            between(emission,0,100)&&between(emissionThreshold,0,kMaxScalar)&&
            bounded(smokeDetailVelocity,100)&&between(smokeDetail,0,1)&&between(emberIntensity,0,10);
    }
};
inline float volumeHash(float3 p){
    const float v=std::sin(p.x*12.9898f+p.y*78.233f+p.z*37.719f)*43758.5453f;
    return v-std::floor(v);
}
inline float volumeNoise(float3 p){
    const float3 base{std::floor(p.x),std::floor(p.y),std::floor(p.z)};
    float3 f{p.x-base.x,p.y-base.y,p.z-base.z};
    f={f.x*f.x*(3.f-2.f*f.x),f.y*f.y*(3.f-2.f*f.y),f.z*f.z*(3.f-2.f*f.z)};
    const auto mix=[](float a,float b,float t){return a+(b-a)*t;};
    const auto plane=[&](float z){
        return mix(mix(volumeHash(base+float3{0,0,z}),volumeHash(base+float3{1,0,z}),f.x),
                   mix(volumeHash(base+float3{0,1,z}),volumeHash(base+float3{1,1,z}),f.x),f.y);
    };
    return mix(plane(0),plane(1),f.z);
}
inline float smokeDetailFactor(float3 p,float strength){
    if(strength<=0)return 1;
    const float low=volumeNoise(p*.95f),high=volumeNoise(p*3.1f);
    return std::max(.025f,1.f+strength*((low-.5f)*4.4f+(high-.5f)*1.3f));
}
inline float flameDetailFactor(float3 p){
    const float low=volumeNoise(p*2.3f),high=volumeNoise(p*6.1f);
    return std::clamp((low*.7f+high*.3f-.26f)*2.15f,0.f,1.f);
}
inline float emberSample(float3 p){
    return volumeHash({std::floor(p.x*8.f),std::floor(p.y*8.f),std::floor(p.z*8.f)});
}
struct RayResult {float3 radiance{};float alpha=0;uint32_t steps=0;};

class GasGrid final {
public:
    bool init(const Config& config){
        if(!config.valid())return false;
        GasGrid next;next.cfg_=config;
        const size_t count=size_t(config.dimension)*config.dimension*config.dimension;
        next.cells_.resize(count);next.scratch_.resize(count);next.solid_.resize(count);
        next.pressure_.resize(count);next.pressureNext_.resize(count);next.divergence_.resize(count);
        *this=std::move(next);return true;
    }
    bool ready()const{return !cells_.empty();}
    const Config& config()const{return cfg_;}
    const std::vector<Cell>& cells()const{return cells_;}
    const std::vector<uint8_t>& solidMask()const{return solid_;}
    uint64_t tick()const{return tick_;}
    size_t memoryBytes()const{
        return (cells_.capacity()+scratch_.capacity())*sizeof(Cell)+solid_.capacity()+
            (pressure_.capacity()+pressureNext_.capacity()+divergence_.capacity())*sizeof(float);
    }
    float3 center(uint32_t x,uint32_t y,uint32_t z)const{
        return cfg_.origin+float3{float(x)+.5f,float(y)+.5f,float(z)+.5f}*cfg_.cellSize;
    }
    bool setSources(const std::vector<Source>& sources){
        if(!ready()||sources.size()>kMaxSources)return false;
        for(const auto& s:sources)if(!s.valid())return false;
        sources_=sources;return true;
    }
    bool setWind(float3 acceleration){
        if(!ready()||!bounded(acceleration,100))return false;
        cfg_.wind=acceleration;return true;
    }
    bool setGravity(float3 gravity){
        if(!ready()||!bounded(gravity,100))return false;
        cfg_.gravity=gravity;return true;
    }
    // Host supplies the same world-space SDF lattice used by particles.
    // Outside the lattice is fluid. Resample once, not once per solver iteration.
    bool setObstacle(const fx::StaticSdfVolume& sdf){
        if(!ready()||!sdf.valid())return false;
        forEach([&](int x,int y,int z,size_t i){
            float distance=0;float3 gradient{};
            solid_[i]=uint8_t(sdf.dimension&&sdf.sample(center(x,y,z),distance,gradient)&&distance<=0);
            if(solid_[i])cells_[i]={};
        });
        return true;
    }
    void clear(){
        std::fill(cells_.begin(),cells_.end(),Cell{});
        std::fill(scratch_.begin(),scratch_.end(),Cell{});
        tick_=0; // obstacle and sources are authored state, not reset
    }
    bool inject(const Source& source,float seconds=1.f){
        if(!ready()||!source.valid()||!between(seconds,0,1))return false;
        addSource(source,seconds);return true;
    }
    // Single fixed visual tick, no frame-time dependence or per-tick allocation.
    // Semi-Lagrangian, first order; approximate collocated Jacobi projection.
    bool step(){
        if(!ready())return false;
        const float3 up=render::length(cfg_.gravity)>1e-6f?render::normalize(-cfg_.gravity):float3{};
        const float vd=std::exp(-cfg_.velocityDecay*kTick);
        forEach([&](int x,int y,int z,size_t i){
            if(solid_[i]){scratch_[i]={};return;}
            const auto p=center(x,y,z);
            Cell c=sampleClamped(p-cells_[i].velocity*kTick);
            const float burn=c.temperature>=cfg_.ignition?
                std::min(c.fuel,c.fuel*cfg_.burnRate*kTick):0;
            c.fuel=std::clamp((c.fuel-burn)*std::exp(-cfg_.fuelDecay*kTick),0.f,kMaxScalar);
            c.temperature=std::clamp((c.temperature+burn*cfg_.heatYield)*std::exp(-cfg_.temperatureDecay*kTick),0.f,kMaxScalar);
            c.density=std::clamp((c.density+burn*cfg_.smokeYield)*std::exp(-cfg_.smokeDecay*kTick),0.f,kMaxScalar);
            c.velocity=limit((c.velocity+(cfg_.wind+up*(cfg_.buoyancy*c.temperature-cfg_.smokeWeight*c.density))*kTick)*vd);
            scratch_[i]=c;
        });
        cells_.swap(scratch_);
        for(const auto& source:sources_)addSource(source.atTick(tick_),kTick);
        enforceWalls();
        if(cfg_.pressureIterations)project();
        enforceWalls();
        ++tick_;return true;
    }
    bool sample(float3 p,Cell& out)const{
        if(!ready()||!finite(p))return false;
        const auto q=(p-cfg_.origin)*(1.f/cfg_.cellSize);
        const float n=float(cfg_.dimension);
        if(q.x<0||q.y<0||q.z<0||q.x>=n||q.y>=n||q.z>=n)return false;
        out=sampleClamped(p);return true;
    }
    Stats stats()const{
        Stats out;out.ticks=tick_;out.cells=cells_.size();
        for(size_t i=0;i<cells_.size();++i){
            out.solidCells+=solid_[i]!=0;
            const auto& c=cells_[i];out.density+=c.density;out.temperature+=c.temperature;out.fuel+=c.fuel;
            out.peakSpeed=std::max(out.peakSpeed,render::length(c.velocity));
        }
        return out;
    }
    // Reference emission/absorption integration of the real simulated field.
    // Host supplies opaque ray distance; returns premultiplied scene-linear
    // radiance and opacity. No postprocess, no lighting/shadow approximation claim.
    bool march(float3 eye,float3 direction,float opaqueDistance,const Material& material,
               RayResult& out,uint32_t maxSteps=128)const{
        if(!ready()||!bounded(eye,1e7f)||!bounded(direction,1e6f)||!between(opaqueDistance,0,1e7f)||
           !material.valid()||!maxSteps||maxSteps>256||render::length(direction)<1e-8f)return false;
        direction=render::normalize(direction);
        const float extent=cfg_.cellSize*float(cfg_.dimension);
        const float3 hi=cfg_.origin+float3{extent,extent,extent};
        float start=0,end=opaqueDistance;
        for(unsigned axis=0;axis<3;++axis){
            const float e=component(eye,axis),d=component(direction,axis);
            const float lo=component(cfg_.origin,axis),upper=component(hi,axis);
            if(std::abs(d)<1e-8f){if(e<lo||e>=upper){out={};return true;}continue;}
            float a=(lo-e)/d,b=(upper-e)/d;if(a>b)std::swap(a,b);
            start=std::max(start,a);end=std::min(end,b);
        }
        RayResult result;
        if(end<=start){out=result;return true;}
        const uint32_t steps=uint32_t(std::clamp(std::ceil((end-start)/(cfg_.cellSize*.5f)),1.f,float(maxSteps)));
        const float ds=(end-start)/float(steps);float trans=1;
        const auto detailOffset=material.smokeDetailVelocity*(float(tick_)*kTick);
        for(uint32_t i=0;i<steps&&trans>1e-4f;++i){
            Cell c;const auto p=eye+direction*(start+(float(i)+.5f)*ds);
            if(!sample(p,c))continue;
            const float sigma=c.density*material.extinction*smokeDetailFactor(p-detailOffset,material.smokeDetail);
            const float opacity=-std::expm1(-sigma*ds);
            const float integral=sigma>1e-6f?opacity/sigma:ds;
            const float heat=std::max(0.f,c.temperature-material.emissionThreshold)*material.emission*flameDetailFactor(p);
            const float ember=std::max(0.f,emberSample(p)-.999f)*1000.f*material.emberIntensity*
                std::min(1.f,c.density)*ds;
            result.radiance=result.radiance+(material.smokeColor*opacity+material.fireColor*(heat*integral)+
                float3{8.f,1.4f,.15f}*ember)*trans;
            trans*=1-opacity;++result.steps;
        }
        result.alpha=1-trans;out=result;return true;
    }
private:
    static float component(float3 p,unsigned axis){return axis==0?p.x:axis==1?p.y:p.z;}
    size_t index(int x,int y,int z)const{
        return (size_t(z)*cfg_.dimension+size_t(y))*cfg_.dimension+size_t(x);
    }
    bool fluid(int x,int y,int z)const{
        const int n=int(cfg_.dimension);
        return x>=0&&y>=0&&z>=0&&x<n&&y<n&&z<n&&!solid_[index(x,y,z)];
    }
    template<class Fn>void forEach(Fn&& fn)const{
        const int n=int(cfg_.dimension);
        for(int z=0;z<n;++z)for(int y=0;y<n;++y)for(int x=0;x<n;++x)fn(x,y,z,index(x,y,z));
    }
    float3 limit(float3 v)const{
        const float len=render::length(v);
        return len>cfg_.maxVelocity?v*(cfg_.maxVelocity/len):v;
    }
    static Cell mix(Cell a,Cell b,float t){
        return {a.velocity+(b.velocity-a.velocity)*t,a.density+(b.density-a.density)*t,
                a.temperature+(b.temperature-a.temperature)*t,a.fuel+(b.fuel-a.fuel)*t};
    }
    Cell sampleClamped(float3 p)const{
        const auto raw=(p-cfg_.origin)*(1.f/cfg_.cellSize)-float3{.5f,.5f,.5f};
        const float upper=float(cfg_.dimension-1);
        const float3 q{std::clamp(raw.x,0.f,upper),std::clamp(raw.y,0.f,upper),std::clamp(raw.z,0.f,upper)};
        const int x=std::min(int(q.x),int(cfg_.dimension)-2),y=std::min(int(q.y),int(cfg_.dimension)-2),z=std::min(int(q.z),int(cfg_.dimension)-2);
        const float u=q.x-x,v=q.y-y,w=q.z-z;
        const auto at=[&](int a,int b,int c){const size_t i=index(a,b,c);return solid_[i]?Cell{}:cells_[i];};
        return mix(mix(mix(at(x,y,z),at(x+1,y,z),u),mix(at(x,y+1,z),at(x+1,y+1,z),u),v),
                   mix(mix(at(x,y,z+1),at(x+1,y,z+1),u),mix(at(x,y+1,z+1),at(x+1,y+1,z+1),u),v),w);
    }
    void addSource(const Source& s,float dt){
        forEach([&](int x,int y,int z,size_t i){
            if(solid_[i])return;
            const auto delta=center(x,y,z)-s.position;
            const float q=render::dot(delta,delta)/(s.radius*s.radius);
            if(q>=1)return;
            const float w=(1-q)*dt;
            auto& c=cells_[i];
            c.density=std::min(kMaxScalar,c.density+s.smoke*w);
            c.temperature=std::min(kMaxScalar,c.temperature+s.temperature*w);
            c.fuel=std::min(kMaxScalar,c.fuel+s.fuel*w);
            const auto radial=render::length(delta)>1e-6f?render::normalize(delta)*s.radialAcceleration:float3{};
            c.velocity=limit(c.velocity+(s.acceleration+radial)*w);
        });
    }
    void enforceWalls(){
        forEach([&](int x,int y,int z,size_t i){
            if(solid_[i]){cells_[i]={};return;}
            auto& v=cells_[i].velocity;
            if((v.x<0&&!fluid(x-1,y,z))||(v.x>0&&!fluid(x+1,y,z)))v.x=0;
            if((v.y<0&&!fluid(x,y-1,z))||(v.y>0&&!fluid(x,y+1,z)))v.y=0;
            if((v.z<0&&!fluid(x,y,z-1))||(v.z>0&&!fluid(x,y,z+1)))v.z=0;
        });
    }
    void project(){
        const float h=cfg_.cellSize;
        const auto vel=[&](int x,int y,int z,unsigned axis){return fluid(x,y,z)?component(cells_[index(x,y,z)].velocity,axis):0.f;};
        std::fill(pressure_.begin(),pressure_.end(),0.f);
        forEach([&](int x,int y,int z,size_t i){
            divergence_[i]=solid_[i]?0.f:(vel(x+1,y,z,0)-vel(x-1,y,z,0)+
                vel(x,y+1,z,1)-vel(x,y-1,z,1)+vel(x,y,z+1,2)-vel(x,y,z-1,2))/(2*h);
        });
        constexpr int offsets[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for(uint32_t iteration=0;iteration<cfg_.pressureIterations;++iteration){
            forEach([&](int x,int y,int z,size_t i){
                if(solid_[i]){pressureNext_[i]=0;return;}
                float sum=0;int count=0;
                for(const auto& o:offsets)if(fluid(x+o[0],y+o[1],z+o[2])){
                    sum+=pressure_[index(x+o[0],y+o[1],z+o[2])];++count;
                }
                pressureNext_[i]=count?(sum-divergence_[i]*h*h)/float(count):0;
            });
            pressure_.swap(pressureNext_);
        }
        forEach([&](int x,int y,int z,size_t i){
            if(solid_[i])return;
            const auto p=[&](int a,int b,int c){return fluid(a,b,c)?pressure_[index(a,b,c)]:pressure_[i];};
            const float3 gradient{p(x+1,y,z)-p(x-1,y,z),p(x,y+1,z)-p(x,y-1,z),p(x,y,z+1)-p(x,y,z-1)};
            cells_[i].velocity=limit(cells_[i].velocity-gradient*(.5f/h));
        });
    }
    Config cfg_{};
    std::vector<Cell> cells_,scratch_;
    std::vector<uint8_t> solid_;
    std::vector<float> pressure_,pressureNext_,divergence_;
    std::vector<Source> sources_;
    uint64_t tick_=0;
};
}
