// EVOLVE: deterministic long-term regional succession and deterioration.
// PHYSIS owns weather, TerraWeave terrain, and the game owns resources.
// EVOLVE stores an independent long-term progression state on selected regions.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::evolve {
using I=std::int64_t;
inline constexpr I kMaxState=1000000000,kMaxRate=1000000;
inline constexpr std::uint32_t kMaxCatchupTicks=10000;
inline constexpr std::size_t kMaxRegions=8192,kMaxPressures=16384;
struct Region {
    I durability=kMaxState,fertility=500000000,succession=0,pollution=0;
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
// External, canonical climate/gameplay producers explicitly set these rates.
// A disabled pressure has no effect; all rates are per fixed simulation tick.
struct Pressure {
    Entity region=kNullEntity;
    I wear=0,recovery=0;
    I enrichment=0,consumption=0;
    I contamination=0,purification=0;
    I growth=0,regression=0;
    bool enabled=true;
};
inline bool inState(I v){return v>=0&&v<=kMaxState;}
inline bool inRate(I v){return v>=0&&v<=kMaxRate;}
inline bool valid(const Region& r){
    return inState(r.durability)&&inState(r.fertility)&&
           inState(r.succession)&&inState(r.pollution);
}
inline bool valid(const Pressure& p){
    return inRate(p.wear)&&inRate(p.recovery)&&
           inRate(p.enrichment)&&inRate(p.consumption)&&
           inRate(p.contamination)&&inRate(p.purification)&&
           inRate(p.growth)&&inRate(p.regression);
}
inline void io(Archive& a,Region& r){
    a(r.durability,r.fertility,r.succession,r.pollution,
      r.enabled,r.sampledTick);
    if(!a.writing()&&!valid(r))a.fail();
}
inline void io(Archive& a,Pressure& p){
    a(p.region,p.wear,p.recovery,p.enrichment,p.consumption,
      p.contamination,p.purification,p.growth,p.regression,p.enabled);
    if(!a.writing()&&!valid(p))a.fail();
}
inline I decrease(I current,I rate,std::uint32_t elapsed){
    // Rate<=1e6, elapsed<=1e4; product<=1e10 and cannot overflow.
    return current-std::min(current,rate*I(elapsed));
}
inline I increase(I current,I rate,std::uint32_t elapsed){
    return current+std::min(kMaxState-current,rate*I(elapsed));
}
// A tick skipped due to clock discontinuity is not silently applied as
// unlimited fast-forward; cap time jumps and preserve source determinism.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Region>()>kMaxRegions||
       read.countExisting<Pressure>()>kMaxPressures)return false;
    std::vector<std::pair<Entity,Region>> regions;
    read.eachExistingConst<Region>([&](Entity e,const Region& r){
        if(read.alive(e))regions.push_back({e,r});
    });
    std::sort(regions.begin(),regions.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(regions.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    std::vector<std::uint32_t> elapsed(regions.size(),0);
    for(std::size_t i=0;i<regions.size();++i){
        const auto& r=regions[i].second;
        if(!valid(r))return false;
        ids.emplace(regions[i].first.packed(),i);
        if(r.sampledTick==tick)continue;
        if(r.sampledTick==UINT32_MAX){elapsed[i]=1;continue;}
        if(tick<r.sampledTick)return false;
        const auto gap=tick-r.sampledTick;
        if(gap>kMaxCatchupTicks)return false;
        elapsed[i]=gap;
    }
    std::vector<std::pair<Entity,Pressure>> pressures;
    read.eachExistingConst<Pressure>([&](Entity e,const Pressure& p){
        if(read.alive(e)&&p.enabled)pressures.push_back({e,p});
    });
    std::sort(pressures.begin(),pressures.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,p]:pressures){
        (void)entity;
        if(!valid(p))return false;
        const auto it=ids.find(p.region.packed());
        if(it==ids.end())continue;
        const std::size_t i=it->second;
        auto& r=regions[i].second;
        if(!r.enabled||!elapsed[i])continue;
        const auto dt=elapsed[i];
        // Apply all sources in stable Entity order; never sum unbounded rates.
        if(!lib::worldprocess::age(r.durability,p.wear,dt))return false;
        r.durability=increase(r.durability,p.recovery,dt);
        r.fertility=decrease(r.fertility,p.consumption,dt);
        r.fertility=increase(r.fertility,p.enrichment,dt);
        r.pollution=decrease(r.pollution,p.purification,dt);
        r.pollution=increase(r.pollution,p.contamination,dt);
        r.succession=decrease(r.succession,p.regression,dt);
        r.succession=increase(r.succession,p.growth,dt);
    }
    if(!world.prepareExistingWrite<Region>())return false;
    for(auto& [entity,region]:regions){
        region.sampledTick=tick;
        world.get<Region>(entity)=region;
    }
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Region>("evolve.region",1);
    sim.registerPersistentComponent<Pressure>("evolve.pressure",1);
}
inline void install(Simulation& sim,std::string name="evolve.succession"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::evolve
