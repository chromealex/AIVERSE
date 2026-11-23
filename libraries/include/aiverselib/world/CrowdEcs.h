// CROWD: deterministic ECS group-slot allocation and bounded local avoidance.
// No renderer/navmesh ownership: game AI supplies candidate slots/targets.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::crowd {
using I=std::int64_t;
inline constexpr I kCoord=1000000,kMaxRadius=2000,kMaxSpeed=1000;
inline constexpr std::size_t kMaxAgents=2048,kMaxSlots=4096,kMaxPreferences=2048;
struct Agent {
    I x=0,z=0,goalX=0,goalZ=0;
    I radius=20,speed=20;
    Entity allocated=kNullEntity;
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Slot {I x=0,z=0;bool enabled=true;};
struct Preference {
    Entity agent=kNullEntity;
    Entity options[4]{kNullEntity,kNullEntity,kNullEntity,kNullEntity};
    bool enabled=true;
};
inline void io(Archive& a,Agent& v){
    a(v.x,v.z,v.goalX,v.goalZ,v.radius,v.speed,v.allocated,v.enabled,v.sampledTick);
    if(!a.writing()&&(v.x < -kCoord||v.x>kCoord||v.z < -kCoord||v.z>kCoord||
       v.goalX < -kCoord||v.goalX>kCoord||v.goalZ < -kCoord||v.goalZ>kCoord||
       v.radius<0||v.radius>kMaxRadius||v.speed<0||v.speed>kMaxSpeed))a.fail();
}
inline void io(Archive& a,Slot& v){
    a(v.x,v.z,v.enabled);
    if(!a.writing()&&(v.x < -kCoord||v.x>kCoord||
       v.z < -kCoord||v.z>kCoord))a.fail();
}
inline void io(Archive& a,Preference& v){a(v.agent,v.options,v.enabled);}
inline bool valid(const Agent& v){
    return v.x>=-kCoord&&v.x<=kCoord&&v.z>=-kCoord&&v.z<=kCoord&&
       v.goalX>=-kCoord&&v.goalX<=kCoord&&v.goalZ>=-kCoord&&v.goalZ<=kCoord&&
       v.radius>=0&&v.radius<=kMaxRadius&&v.speed>=0&&v.speed<=kMaxSpeed;
}
inline bool valid(const Slot& v){
    return v.x>=-kCoord&&v.x<=kCoord&&v.z>=-kCoord&&v.z<=kCoord;
}
inline I approach(I a,I b,I speed){
    if(a<b)return a+std::min(speed,b-a);
    if(a>b)return a-std::min(speed,a-b);
    return a;
}
// Deterministic first-come reservation with a per-agent preferred slot list;
// avoidance only does local one-step collision rejection, not global RVO.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Agent>()>kMaxAgents||
       read.countExisting<Slot>()>kMaxSlots||
       read.countExisting<Preference>()>kMaxPreferences)return false;
    std::vector<std::pair<Entity,Agent>> agents;
    read.eachExistingConst<Agent>([&](Entity e,const Agent& a){
        if(read.alive(e))agents.push_back({e,a});
    });
    std::sort(agents.begin(),agents.end(),
        [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::vector<std::pair<Entity,Slot>> slots;
    read.eachExistingConst<Slot>([&](Entity e,const Slot& s){
        if(read.alive(e))slots.push_back({e,s});
    });
    std::sort(slots.begin(),slots.end(),
        [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::map<std::uint64_t,std::size_t> slotIds;
    for(std::size_t i=0;i<slots.size();++i){
        if(!valid(slots[i].second))return false;
        if(slots[i].second.enabled)slotIds.emplace(slots[i].first.packed(),i);
    }
    std::map<std::uint64_t,std::vector<std::size_t>> preferred;
    std::vector<std::pair<Entity,Preference>> preferences;
    read.eachExistingConst<Preference>([&](Entity e,const Preference& p){
        if(read.alive(e)&&p.enabled&&read.alive(p.agent))
            preferences.push_back({e,p});
    });
    std::sort(preferences.begin(),preferences.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,p]:preferences){
        (void)entity;
        auto& vec=preferred[p.agent.packed()];
        for(Entity slot:p.options){
            const auto it=slotIds.find(slot.packed());
            if(it!=slotIds.end()&&
               std::find(vec.begin(),vec.end(),it->second)==vec.end())
                vec.push_back(it->second);
        }
    }
    bool all=true,any=false;
    for(const auto& [e,a]:agents){
        (void)e;
        if(!valid(a))return false;
        all &= a.sampledTick==tick;
        any |= a.sampledTick==tick;
    }
    if(agents.empty()||all)return true;
    if(any)return false;
    std::vector<std::vector<std::size_t>> priorities(agents.size());
    for(std::size_t i=0;i<agents.size();++i)
        if(auto it=preferred.find(agents[i].first.packed());it!=preferred.end())
            priorities[i]=it->second;
    const auto assignment=lib::worldprocess::assignSlots(priorities,slots.size());
    std::vector<std::array<I,2>> next;
    next.reserve(agents.size());
    for(std::size_t i=0;i<agents.size();++i){
        auto& a=agents[i].second;
        if(!a.enabled){a.sampledTick=tick;next.push_back({a.x,a.z});continue;}
        I gx=a.goalX,gz=a.goalZ;
        a.allocated=kNullEntity;
        if(assignment[i]>=0){
            const auto j=std::size_t(assignment[i]);
            gx=slots[j].second.x;gz=slots[j].second.z;
            a.allocated=slots[j].first;
        }
        const I x=approach(a.x,gx,a.speed);
        const I z=approach(a.z,gz,a.speed);
        bool clear=true;
        for(std::size_t j=0;j<agents.size();++j){
            if(i==j||!agents[j].second.enabled)continue;
            // Already-processed agents have their intended new position;
            // unprocessed ones use their original position.
            const I ox=j<i?next[j][0]:agents[j].second.x;
            const I oz=j<i?next[j][1]:agents[j].second.z;
            const I dx=x-ox,dz=z-oz;
            const I r=a.radius+agents[j].second.radius;
            // Coordinates bounded to ±1e6; squares within int64.
            if(dx*dx+dz*dz<r*r){clear=false;break;}
        }
        if(clear){a.x=x;a.z=z;}
        a.sampledTick=tick;
        next.push_back({a.x,a.z});
    }
    if(!world.prepareExistingWrite<Agent>())return false;
    for(const auto& [e,a]:agents)world.get<Agent>(e)=a;
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Agent>("crowd.agent",1);
    sim.registerPersistentComponent<Slot>("crowd.slot",1);
    sim.registerPersistentComponent<Preference>("crowd.preference",1);
}
inline void install(Simulation& sim,std::string name="crowd.avoidance"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::crowd
