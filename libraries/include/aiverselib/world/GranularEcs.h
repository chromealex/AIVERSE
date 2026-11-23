// GRANULAR: deterministic bounded cellular solid settling and deposition.
// Owns quantized grains only. TerraWeave owns terrain; Box3D owns rigid bodies.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::granular {
using I=std::int64_t;
inline constexpr std::size_t kMaxFields=16,kMaxSources=4096;
inline constexpr std::uint16_t kMaxDimension=128;
inline constexpr I kMaxCellMass=1000000,kMaxSourceRate=100000;
struct Field {
    std::uint16_t width=1,height=1;
    I capacity=1000;
    std::vector<I> mass{0};
    std::vector<std::uint8_t> solid{0}; // 1=blocking boundary
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Source {
    Entity field=kNullEntity;
    std::uint16_t x=0,y=0;
    I delta=0; // +deposition, -excavation; owning game decides the source
    bool enabled=true;
};
inline bool valid(const Field& f){
    if(f.width==0||f.height==0||
       f.width>kMaxDimension||f.height>kMaxDimension||
       f.capacity<1||f.capacity>kMaxCellMass)return false;
    const auto size=std::size_t(f.width)*f.height;
    if(f.mass.size()!=size||f.solid.size()!=size)return false;
    for(std::size_t i=0;i<size;++i)
        if(f.mass[i]<0||f.mass[i]>f.capacity||
           f.solid[i]>1|| (f.solid[i]&&f.mass[i]!=0))return false;
    return true;
}
inline bool valid(const Source& s){
    return s.delta>=-kMaxSourceRate&&s.delta<=kMaxSourceRate;
}
inline void io(Archive& a,Field& f){
    a(f.width,f.height,f.capacity,f.mass,f.solid,f.enabled,f.sampledTick);
    if(!a.writing()&&!valid(f))a.fail();
}
inline void io(Archive& a,Source& s){
    a(s.field,s.x,s.y,s.delta,s.enabled);
    if(!a.writing()&&!valid(s))a.fail();
}
// Moves mass by gravity down one row per tick (or diagonal-down if a cell is
// blocked/full); update order is stable and movement conserves solids except
// explicitly authored sources. This is 2D gameplay granules, not full SPH.
inline bool settle(Field& f,std::uint32_t tick){
    if(!valid(f))return false;
    if(!f.enabled){f.sampledTick=tick;return true;}
    const std::size_t w=f.width,h=f.height;
    if(h<2){f.sampledTick=tick;return true;}
    auto& m=f.mass;
    const auto& solid=f.solid;
    const auto index=[w](std::size_t x,std::size_t y){return y*w+x;};
    for(std::size_t y=h-1;y>0;--y){
        for(std::size_t x=0;x<w;++x){
            const auto src=index(x,y-1);
            if(solid[src]||!m[src])continue;
            const auto transfer=[&](std::size_t xx,std::size_t yy){
                const auto dst=index(xx,yy);
                if(solid[dst])return;
                const I amount=std::min(m[src],f.capacity-m[dst]);
                m[src]-=amount;m[dst]+=amount;
            };
            transfer(x,y);
            if(m[src]){
                // Deterministic parity prevents a permanent left-side bias.
                const bool left=((tick+std::uint32_t(y)+std::uint32_t(x))&1)==0;
                if(left){
                    if(x>0)transfer(x-1,y);
                    if(x+1<w)transfer(x+1,y);
                }else{
                    if(x+1<w)transfer(x+1,y);
                    if(x>0)transfer(x-1,y);
                }
            }
        }
    }
    f.sampledTick=tick;
    return true;
}
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Field>()>kMaxFields||
       read.countExisting<Source>()>kMaxSources)return false;
    std::vector<std::pair<Entity,Field>> fields;
    read.eachExistingConst<Field>([&](Entity e,const Field& f){
        if(read.alive(e))fields.push_back({e,f});
    });
    std::sort(fields.begin(),fields.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(fields.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<fields.size();++i){
        if(!valid(fields[i].second))return false;
        ids.emplace(fields[i].first.packed(),i);
        all &= fields[i].second.sampledTick==tick;
        any |= fields[i].second.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<std::pair<Entity,Source>> sources;
    read.eachExistingConst<Source>([&](Entity e,const Source& s){
        if(read.alive(e)&&s.enabled)sources.push_back({e,s});
    });
    std::sort(sources.begin(),sources.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,s]:sources){
        (void)entity;
        if(!valid(s))return false;
        const auto it=ids.find(s.field.packed());
        if(it==ids.end())continue; // stale generation never rebinds
        auto& field=fields[it->second].second;
        if(!field.enabled||s.x>=field.width||s.y>=field.height)return false;
        const std::size_t offset=std::size_t(s.y)*field.width+s.x;
        if(field.solid[offset])return false;
        const I amount=field.mass[offset];
        if(s.delta<0 && amount< -s.delta)return false;
        if(s.delta>0 && amount>field.capacity-s.delta)return false;
        field.mass[offset]=amount+s.delta;
    }
    for(auto& [entity,field]:fields){
        (void)entity;
        if(!settle(field,tick))return false;
    }
    if(!world.prepareExistingWrite<Field>())return false;
    for(const auto& [entity,field]:fields)world.get<Field>(entity)=field;
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Field>("granular.field",1);
    sim.registerPersistentComponent<Source>("granular.source",1);
}
inline void install(Simulation& sim,std::string name="granular.settle"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::granular
