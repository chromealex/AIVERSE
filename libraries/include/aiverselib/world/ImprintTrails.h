// IMPRINT: persistent movement-to-trail emitters for any genre's actors.
// The existing movement/NavMesh/Box3D host owns positions and calls
// sampleMotion() at an authoritative ECS barrier. This module only
// converts validated movement into trace deposits on canonical IMPRINT cells.
#pragma once
#include "ImprintEcs.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::imprint {
inline constexpr std::size_t kMaxTrackers=8192;
struct Tracker {
    Entity source=kNullEntity,cell=kNullEntity;
    I lastX=0,lastZ=0,currentX=0,currentZ=0;
    I minimumDistance=25; // centimetres, integer; 0 = always emit once/tick
    I footprint=0,scent=0,residue=0;
    std::uint32_t motionTick=UINT32_MAX;
    std::uint32_t emittedTick=UINT32_MAX;
    bool initialized=false,enabled=true;
};
inline bool valid(const Tracker& v){
    constexpr I kCoord=1000000;
    return v.lastX>=-kCoord&&v.lastX<=kCoord&&
        v.lastZ>=-kCoord&&v.lastZ<=kCoord&&
        v.currentX>=-kCoord&&v.currentX<=kCoord&&
        v.currentZ>=-kCoord&&v.currentZ<=kCoord&&
        v.minimumDistance>=0&&v.minimumDistance<=kCoord&&
        inRate(v.footprint)&&inRate(v.scent)&&inRate(v.residue);
}
inline void io(Archive& a,Tracker& v){
    a(v.source,v.cell,v.lastX,v.lastZ,v.currentX,v.currentZ,
      v.minimumDistance,v.footprint,v.scent,v.residue,v.motionTick,
      v.emittedTick,v.initialized,v.enabled);
    if(!a.writing()&&!valid(v))a.fail();
}
// Record one authoritative position sample. Repeated sample for one tick
// replaces the pending motion snapshot, never applies an extra footprint.
// Source and target are generation-safe and must remain valid at emission.
inline bool sampleMotion(World& world,Entity tracker,
                         std::uint32_t tick,I x,I z){
    if(!world.alive(tracker)||x< -1000000||x>1000000||
       z< -1000000||z>1000000)return false;
    const auto* current=static_cast<const World&>(world)
                         .tryGetConst<Tracker>(tracker);
    if(!current||!valid(*current)||!current->enabled||
       current->emittedTick==tick||
       (current->motionTick!=UINT32_MAX&&tick<current->motionTick))
        return false;
    auto& writable=world.get<Tracker>(tracker);
    writable.currentX=x;
    writable.currentZ=z;
    writable.motionTick=tick;
    return true;
}
// Must execute after movement producer data is sampled but before the
// normal imprint::advance; it stages every trace update atomically.
inline bool stampMotion(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Tracker>()>kMaxTrackers)return false;
    std::vector<std::pair<Entity,Tracker>> tracks;
    read.eachExistingConst<Tracker>([&](Entity e,const Tracker& v){
        if(read.alive(e))tracks.push_back({e,v});
    });
    std::sort(tracks.begin(),tracks.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::map<std::uint64_t,Cell> destinations;
    for(auto& [entity,t]:tracks){
        (void)entity;
        if(!valid(t))return false;
        if(!t.enabled||t.emittedTick==tick)continue;
        if(t.motionTick!=tick)continue;
        if(!read.alive(t.source)||!read.alive(t.cell))return false;
        const auto* cell=read.tryGetConst<Cell>(t.cell);
        if(!cell||!cell->enabled||!valid(*cell))return false;
        if(!t.initialized){
            t.lastX=t.currentX;t.lastZ=t.currentZ;
            t.initialized=true;
            t.emittedTick=tick;
            continue;
        }
        const I dx=t.currentX-t.lastX,dz=t.currentZ-t.lastZ;
        if(dx*dx+dz*dz<t.minimumDistance*t.minimumDistance)continue;
        const auto key=t.cell.packed();
        const auto it=destinations.try_emplace(key,*cell).first;
        auto& target=it->second;
        target.footprint=std::min(kMaxLevel,target.footprint+t.footprint);
        target.scent=std::min(kMaxLevel,target.scent+t.scent);
        target.residue=std::min(kMaxLevel,target.residue+t.residue);
        t.lastX=t.currentX;t.lastZ=t.currentZ;
        t.emittedTick=tick;
    }
    if(!tracks.empty()&&!world.prepareExistingWrite<Tracker>())return false;
    if(!destinations.empty()&&!world.prepareExistingWrite<Cell>())return false;
    for(const auto& [entity,track]:tracks)world.get<Tracker>(entity)=track;
    for(const auto& [key,updated]:destinations)
        world.get<Cell>(Entity::fromPacked(key))=updated;
    return true;
}
inline void installTrails(Simulation& sim,
                          std::string name="imprint.motion_trails"){
    sim.registerPersistentComponent<Tracker>("imprint.tracker",1);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)stampMotion(s.world(),s.tick());
    });
}
} // namespace aiverse::imprint
