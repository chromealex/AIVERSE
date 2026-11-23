// FRACTURE: persistent ECS support-graph collapse, not a second physics/damage engine.
// PHYSIS owns environmental stress and its fracture outbox; Box3D/TerraWeave own actual motion.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::fracture {
inline constexpr std::size_t kMaxMembers=4096;
inline constexpr std::size_t kMaxBonds=16384;
inline constexpr std::int64_t kMaxBondStrength=1000000000;

// Attached to any scene member/entity; enabled topology is an opt-in capability.
// `broken` is set ONLY by the host's authoritative damage/PHYSIS adapter.
// `detached` is a latched authoritative structural outcome. Reattachment is
// an explicit trusted repair transaction, not an automatic graph recomputation.
struct Member {
    bool anchored=false;
    bool broken=false;
    bool detached=false;
};
struct Bond {
    Entity first=kNullEntity,second=kNullEntity;
    std::int64_t strength=1; // authoring units, strictly positive
    std::int64_t damage=0;   // 0..strength; >=strength means severed
    bool enabled=true;
};
enum class Delivery:std::uint8_t {Empty=0,Pending=1,Acknowledged=2};
// Optional durable output on each Member entity. Not the PHYSIS outbox: this
// outbox records a structural SUPPORT LOSS, not the original stress fracture.
struct Collapse {
    std::uint64_t eventKey=0;
    std::uint32_t detachedTick=0;
    Delivery delivery=Delivery::Empty;
};
struct Event {
    Entity entity=kNullEntity;
    std::uint64_t key=0;
    std::uint32_t tick=0;
};

inline void io(Archive& a,Member& v){a(v.anchored,v.broken,v.detached);}
inline void io(Archive& a,Bond& v){
    a(v.first,v.second,v.strength,v.damage,v.enabled);
    if(!a.writing()&&(v.strength<=0||v.strength>kMaxBondStrength||
                       v.damage<0||v.damage>v.strength))a.fail();
}
inline void io(Archive& a,Collapse& v){
    a(v.eventKey,v.detachedTick,v.delivery);
    if(!a.writing()&&(static_cast<std::uint8_t>(v.delivery)>2||
        (v.delivery!=Delivery::Empty&&v.eventKey==0)))a.fail();
}
inline std::uint64_t collapseEventKey(Entity e,std::uint32_t tick){
    Hasher hasher;
    hasher.u64(0x4652414354555245ull); // "FRACTURE"
    hasher.u64(e.packed());
    hasher.u32(tick);
    const std::uint64_t key=hasher.value();
    return key?key:1;
}
inline bool validBond(const Bond& bond){
    return bond.enabled && bond.first!=bond.second &&
           bond.strength>0 && bond.strength<=kMaxBondStrength &&
           bond.damage>=0 && bond.damage<bond.strength;
}

// System stage: reads Members/Bonds, writes Member.detached and optional Collapse
// output. No entity creation, no global mutable graph, no RNG or float.
// The host should invoke it after authoritative damage/PHYSIS and before the
// host's durable detach + Box3D processing barrier.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Member>()>kMaxMembers ||
       read.countExisting<Bond>()>kMaxBonds)return false;

    std::vector<std::pair<Entity,Member>> members;
    read.eachExistingConst<Member>([&](Entity e,const Member& m){
        if(read.alive(e))members.emplace_back(e,m);
    });
    std::sort(members.begin(),members.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    std::map<std::uint64_t,std::size_t> indices;
    std::vector<bool> roots(members.size(),false);
    for(std::size_t i=0;i<members.size();++i){
        indices.emplace(members[i].first.packed(),i);
        const auto& m=members[i].second;
        roots[i]=m.anchored&&!m.broken&&!m.detached;
    }
    std::vector<std::pair<Entity,Bond>> bonds;
    read.eachExistingConst<Bond>([&](Entity e,const Bond& b){
        if(read.alive(e)&&validBond(b))bonds.emplace_back(e,b);
    });
    std::sort(bonds.begin(),bonds.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    std::vector<lib::worldprocess::Edge> edges;
    edges.reserve(bonds.size());
    for(const auto& [entity,b]:bonds){
        (void)entity;
        const auto a=indices.find(b.first.packed()),z=indices.find(b.second.packed());
        if(a==indices.end()||z==indices.end())continue;
        if(members[a->second].second.broken||members[z->second].second.broken||
           members[a->second].second.detached||members[z->second].second.detached)continue;
        edges.push_back({a->second,z->second,b.strength-b.damage});
    }
    const auto connected=lib::worldprocess::supported(members.size(),roots,edges);
    if(connected.size()!=members.size())return false;

    // All planning is finished before any authoritative writes.
    auto* write=world.prepareExistingWrite<Member>();
    if(!write)return true;
    for(std::size_t i=0;i<members.size();++i){
        const Entity e=members[i].first;
        const bool shouldDetach=members[i].second.broken||!connected[i];
        if(!shouldDetach||members[i].second.detached)continue;
        world.get<Member>(e).detached=true;
        if(read.tryGetConst<Collapse>(e)){
            auto& outbox=world.get<Collapse>(e);
            // Do not overwrite an outstanding/acknowledged event. A repair
            // must explicitly reconcile the old outbox first.
            if(outbox.delivery==Delivery::Empty){
                outbox.eventKey=collapseEventKey(e,tick);
                outbox.detachedTick=tick;
                outbox.delivery=Delivery::Pending;
            }
        }
    }
    return true;
}
inline std::vector<Event> pending(const World& world){
    std::vector<Event> events;
    world.eachExistingConst<Collapse>([&](Entity e,const Collapse& c){
        if(world.alive(e)&&world.has<Member>(e)&&
           world.tryGetConst<Member>(e)->detached&&
           c.delivery==Delivery::Pending&&c.eventKey)
            events.push_back({e,c.eventKey,c.detachedTick});
    });
    std::sort(events.begin(),events.end(),[](const Event& a,const Event& b){
        return a.entity.packed()<b.entity.packed();
    });
    return events;
}
// Acknowledge ONLY after recording the physical/terrain consequence durably.
// Wrong generation, key, tick or duplicate confirmation leave ECS unchanged.
inline bool acknowledge(World& world,const Event& event){
    if(!world.alive(event.entity))return false;
    const auto& read=static_cast<const World&>(world);
    const auto* member=read.tryGetConst<Member>(event.entity);
    const auto* box=read.tryGetConst<Collapse>(event.entity);
    if(!member||!member->detached||!box||box->delivery!=Delivery::Pending||
       box->eventKey!=event.key||box->detachedTick!=event.tick)return false;
    world.get<Collapse>(event.entity).delivery=Delivery::Acknowledged;
    return true;
}
// Trusted repair/reconstruction transition. The host must first rebuild
// physical objects, repair relevant Bonds and clear any PHYSIS stress latch.
// Preserve old event identity as a tombstone; the old event cannot be re-acked.
// Reusing a collapsed member inside the original tick is forbidden.
inline bool rearmAfterRepair(World& world,const Event& acknowledged,
                             std::uint32_t currentTick){
    if(!world.alive(acknowledged.entity))return false;
    const World& read=world;
    const auto* member=read.tryGetConst<Member>(acknowledged.entity);
    const auto* box=read.tryGetConst<Collapse>(acknowledged.entity);
    if(!member||!member->detached||!box||
       box->delivery!=Delivery::Acknowledged ||
       box->eventKey!=acknowledged.key ||
       box->detachedTick!=acknowledged.tick ||
       currentTick<=box->detachedTick)return false;
    auto& mutableMember=world.get<Member>(acknowledged.entity);
    mutableMember.detached=false;
    mutableMember.broken=false;
    // Keep `eventKey` and `detachedTick` so old confirmations remain invalid.
    world.get<Collapse>(acknowledged.entity).delivery=Delivery::Empty;
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Member>("fracture.member",1);
    sim.registerPersistentComponent<Bond>("fracture.bond",1);
    sim.registerPersistentComponent<Collapse>("fracture.collapse",1);
}
inline void install(Simulation& sim,std::string name="fracture.support"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)advance(s.world(),s.tick());
    });
}
} // namespace aiverse::fracture
