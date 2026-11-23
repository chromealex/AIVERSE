// AETHER: deterministic multi-species gas transport between ECS cell entities.
// PHYSIS remains the authoritative owner of wind/climate/fire. No renderer code.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::aether {
using Mass=std::int64_t;
inline constexpr Mass kMaxCellMass=1000000000;
inline constexpr Mass kMaxDuctRate=10000000;
inline constexpr std::size_t kMaxCells=8192;
inline constexpr std::size_t kMaxDucts=32768;
inline constexpr std::size_t kMaxFluxes=8192;

// Arbitrary nonnegative game-quantized mass in equal-volume gas cells.
// All output-affecting gas species and clocks remain authoritative ECS data.
// Never overwrite a PHYSIS Atmosphere component or duplicate weather.
struct GasCell {
    Mass smoke=0, toxin=0, oxygen=0;
    std::uint32_t sampledTick=std::numeric_limits<std::uint32_t>::max();
    bool enabled=true;
};
// A duct allows concentration-driven diffusion. Opening a door can enable the
// duct. The direction restriction is a *one-way valve*, not wind advection.
struct GasDuct {
    Entity from=kNullEntity,to=kNullEntity;
    Mass maximumPerTick=0;
    bool oneWay=false,enabled=true;
};
// Positive source or negative sink applied after diffusion each tick.
// A source can be placed on any ECS entity and target a generation-safe cell.
struct GasFlux {
    Entity cell=kNullEntity;
    Mass smoke=0,toxin=0,oxygen=0;
    bool enabled=true;
};
inline void io(Archive& a,GasCell& value){
    a(value.smoke,value.toxin,value.oxygen,value.sampledTick,value.enabled);
    if(!a.writing() && (value.smoke<0||value.smoke>kMaxCellMass||
                        value.toxin<0||value.toxin>kMaxCellMass||
                        value.oxygen<0||value.oxygen>kMaxCellMass))a.fail();
}
inline void io(Archive& a,GasDuct& value){
    a(value.from,value.to,value.maximumPerTick,value.oneWay,value.enabled);
    if(!a.writing() && (value.maximumPerTick<0||
                        value.maximumPerTick>kMaxDuctRate))a.fail();
}
inline void io(Archive& a,GasFlux& value){
    a(value.cell,value.smoke,value.toxin,value.oxygen,value.enabled);
    if(!a.writing() && (value.smoke< -kMaxDuctRate||
                        value.smoke>kMaxDuctRate||
                        value.toxin< -kMaxDuctRate||
                        value.toxin>kMaxDuctRate||
                        value.oxygen< -kMaxDuctRate||
                        value.oxygen>kMaxDuctRate))a.fail();
}
inline bool inRange(Mass v){return v>=0&&v<=kMaxCellMass;}
inline bool valid(const GasCell& c){
    return inRange(c.smoke)&&inRange(c.toxin)&&inRange(c.oxygen);
}
inline bool valid(const GasDuct& d){
    return d.maximumPerTick>=0 && d.maximumPerTick<=kMaxDuctRate;
}
inline bool valid(const GasFlux& s){
    return s.smoke>= -kMaxDuctRate&&s.smoke<=kMaxDuctRate &&
           s.toxin>= -kMaxDuctRate&&s.toxin<=kMaxDuctRate &&
           s.oxygen>= -kMaxDuctRate&&s.oxygen<=kMaxDuctRate;
}
inline bool addDelta(Mass& current,Mass delta){
    // Both operands are validated within small bounded ranges; subtracting
    // first in signed arithmetic cannot overflow.
    if(delta<0 && current< -delta)return false;
    if(delta>0 && current>kMaxCellMass-delta)return false;
    current+=delta;
    return inRange(current);
}

// Read: GasCell, GasDuct, GasFlux. Write: GasCell ONLY.
// Stages all three species + sources transactionally; a failed budget,
// invalid mass or invalid active flux does not touch authoritative data.
// Stable packed Entity ordering makes results independent of pool layout.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<GasCell>()>kMaxCells ||
       read.countExisting<GasDuct>()>kMaxDucts ||
       read.countExisting<GasFlux>()>kMaxFluxes)return false;
    std::vector<Entity> cells;
    read.eachExistingConst<GasCell>([&](Entity e,const GasCell&){
        if(read.alive(e))cells.push_back(e);
    });
    std::sort(cells.begin(),cells.end(),
        [](Entity a,Entity b){return a.packed()<b.packed();});
    if(cells.empty())return true;
    std::map<std::uint64_t,std::size_t> indices;
    std::vector<Mass> smoke,toxin,oxygen;
    std::vector<bool> enabled;
    smoke.reserve(cells.size());toxin.reserve(cells.size());
    oxygen.reserve(cells.size());enabled.reserve(cells.size());
    bool allDone=true,anyDone=false;
    for(std::size_t i=0;i<cells.size();++i){
        const auto& g=*read.tryGetConst<GasCell>(cells[i]);
        if(!valid(g))return false;
        indices.emplace(cells[i].packed(),i);
        smoke.push_back(g.smoke);toxin.push_back(g.toxin);oxygen.push_back(g.oxygen);
        enabled.push_back(g.enabled);
        allDone &= g.sampledTick==tick;
        anyDone |= g.sampledTick==tick;
    }
    if(allDone)return true;
    // An interrupted or partially registered same-tick update is rejected;
    // retry on a fresh tick, never diffuse a subset of the network twice.
    if(anyDone)return false;
    std::vector<std::pair<Entity,GasDuct>> ducts;
    read.eachExistingConst<GasDuct>([&](Entity e,const GasDuct& d){
        if(read.alive(e)&&d.enabled)ducts.push_back({e,d});
    });
    std::sort(ducts.begin(),ducts.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    std::vector<lib::worldprocess::Flow> flows;
    flows.reserve(ducts.size()*2);
    for(const auto& [entity,d]:ducts){
        (void)entity;
        if(!valid(d))return false;
        const auto a=indices.find(d.from.packed()),b=indices.find(d.to.packed());
        if(a==indices.end()||b==indices.end()||a==b||
           !enabled[a->second]||!enabled[b->second])continue;
        flows.push_back({a->second,b->second,d.maximumPerTick});
        if(!d.oneWay)flows.push_back({b->second,a->second,d.maximumPerTick});
    }
    if(!lib::worldprocess::diffuse(smoke,flows) ||
       !lib::worldprocess::diffuse(toxin,flows) ||
       !lib::worldprocess::diffuse(oxygen,flows))return false;

    std::vector<std::pair<Entity,GasFlux>> sources;
    read.eachExistingConst<GasFlux>([&](Entity e,const GasFlux& f){
        if(read.alive(e)&&f.enabled)sources.push_back({e,f});
    });
    std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    for(const auto& [entity,source]:sources){
        (void)entity;
        if(!valid(source))return false;
        const auto idx=indices.find(source.cell.packed());
        if(idx==indices.end()||!enabled[idx->second])continue;
        const auto i=idx->second;
        if(!addDelta(smoke[i],source.smoke)||
           !addDelta(toxin[i],source.toxin)||
           !addDelta(oxygen[i],source.oxygen))return false;
    }
    for(std::size_t i=0;i<cells.size();++i)
        if(!inRange(smoke[i])||!inRange(toxin[i])||!inRange(oxygen[i]))
            return false;
    auto* writable=world.prepareExistingWrite<GasCell>();
    if(!writable)return false;
    for(std::size_t i=0;i<cells.size();++i){
        auto& gas=world.get<GasCell>(cells[i]);
        gas.smoke=smoke[i];gas.toxin=toxin[i];gas.oxygen=oxygen[i];
        gas.sampledTick=tick;
    }
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<GasCell>("aether.gas_cell",1);
    sim.registerPersistentComponent<GasDuct>("aether.duct",1);
    sim.registerPersistentComponent<GasFlux>("aether.flux",1);
}
inline void install(Simulation& sim,std::string name="aether.transport"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)advance(s.world(),s.tick());
    });
}
} // namespace aiverse::aether
