// CIRCUIT: opt-in deterministic ECS power routing and one-tick logic signals.
// Authoritative game economy / electrical supply stays in its owning module;
// this graph reads explicitly provided generator supplies and loads only.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include "engine/core/GraphPowerAllocation.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::circuit {
using I=std::int64_t;
inline constexpr I kMaxUnits=1000000;
inline constexpr std::size_t kMaxNodes=4096,kMaxWires=16384,kMaxGates=8192;
enum class Op:std::uint8_t {And=0,Or=1,Xor=2,Not=3};
struct Node {
    I supply=0,demand=0; // supply>0 marks generator, demand is optional load
    bool drive=false,powered=false,signal=false,enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Wire {
    Entity from=kNullEntity,to=kNullEntity;
    bool bidirectional=true,enabled=true;
};
struct Gate {
    Entity inputA=kNullEntity,inputB=kNullEntity,output=kNullEntity;
    Op operation=Op::And;
    bool enabled=true;
};
inline bool valid(const Node& n){
    return n.supply>=0&&n.supply<=kMaxUnits&&
           n.demand>=0&&n.demand<=kMaxUnits;
}
inline void io(Archive& a,Node& n){
    a(n.supply,n.demand,n.drive,n.powered,n.signal,n.enabled,n.sampledTick);
    if(!a.writing()&&!valid(n))a.fail();
}
inline void io(Archive& a,Wire& w){a(w.from,w.to,w.bidirectional,w.enabled);}
inline void io(Archive& a,Gate& g){
    a(g.inputA,g.inputB,g.output,g.operation,g.enabled);
    if(!a.writing()&&static_cast<std::uint8_t>(g.operation)>3)a.fail();
}
inline bool logic(Op op,bool a,bool b){
    switch(op){
        case Op::And:return a&&b;
        case Op::Or:return a||b;
        case Op::Xor:return a!=b;
        case Op::Not:return !a;
    }
    return false;
}
// Producers own supply/demand and input drive. Output \`powered/signal\` is
// calculated here. No dynamic entities or nondeterministic circuit settling.
struct PowerCache {
    std::vector<uint64_t> memberIds;
    lib::GraphPowerAllocation allocation;
};
inline bool advance(World& world,std::uint32_t tick,PowerCache& cache,
                    lib::PowerAllocationActivity* activity=nullptr){
    const World& read=world;
    if(read.countExisting<Node>()>kMaxNodes||
       read.countExisting<Wire>()>kMaxWires||
       read.countExisting<Gate>()>kMaxGates)return false;
    std::vector<std::pair<Entity,Node>> nodes;
    read.eachExistingConst<Node>([&](Entity e,const Node& n){
        if(read.alive(e))nodes.push_back({e,n});
    });
    std::sort(nodes.begin(),nodes.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(nodes.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<nodes.size();++i){
        if(!valid(nodes[i].second))return false;
        ids.emplace(nodes[i].first.packed(),i);
        all &= nodes[i].second.sampledTick==tick;
        any |= nodes[i].second.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<std::pair<Entity,Wire>> wires;
    read.eachExistingConst<Wire>([&](Entity e,const Wire& w){
        if(read.alive(e)&&w.enabled)wires.push_back({e,w});
    });
    std::sort(wires.begin(),wires.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::vector<std::vector<std::size_t>> adjacent(nodes.size());
    for(const auto& [entity,w]:wires){
        (void)entity;
        const auto a=ids.find(w.from.packed()),b=ids.find(w.to.packed());
        if(a==ids.end()||b==ids.end()||a==b)continue;
        if(!nodes[a->second].second.enabled||!nodes[b->second].second.enabled)continue;
        adjacent[a->second].push_back(b->second);
        if(w.bidirectional)adjacent[b->second].push_back(a->second);
    }
    std::vector<uint64_t> memberIds;
    std::vector<lib::PowerVertex> inputs;
    memberIds.reserve(nodes.size());inputs.reserve(nodes.size());
    for(const auto& [e,n]:nodes){
        memberIds.push_back(e.packed());
        inputs.push_back({n.supply,n.demand,n.enabled});
    }
    if(cache.memberIds!=memberIds){
        cache.allocation.reset();cache.memberIds=std::move(memberIds);
    }
    lib::PowerAllocationActivity stats{};
    const auto allocation=cache.allocation.update(inputs,std::move(adjacent),&stats);
    if(!allocation)return false;
    const auto& powered=*allocation;
    std::vector<bool> nextSignal(nodes.size(),false);
    for(std::size_t i=0;i<nodes.size();++i)
        nextSignal[i]=powered[i]&&nodes[i].second.drive;
    std::vector<std::pair<Entity,Gate>> gates;
    read.eachExistingConst<Gate>([&](Entity e,const Gate& g){
        if(read.alive(e)&&g.enabled)gates.push_back({e,g});
    });
    std::sort(gates.begin(),gates.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::vector<bool> gateSet(nodes.size(),false);
    for(const auto& [entity,g]:gates){
        (void)entity;
        if(static_cast<std::uint8_t>(g.operation)>3)return false;
        const auto output=ids.find(g.output.packed());
        if(output==ids.end())continue;
        const auto a=ids.find(g.inputA.packed()),b=ids.find(g.inputB.packed());
        // A stale/disconnected input must not create a NOT=true output:
        // refuse to drive the destination, preserving fail-closed semantics.
        if(a==ids.end()||!nodes[a->second].second.enabled||
           (g.operation!=Op::Not &&
             (b==ids.end()||!nodes[b->second].second.enabled)))continue;
        const std::size_t i=output->second;
        if(gateSet[i])return false; // duplicate owner is not deterministic authoring
        gateSet[i]=true;
        const bool av=nodes[a->second].second.signal;
        const bool bv=b!=ids.end()&&nodes[b->second].second.enabled&&
                      nodes[b->second].second.signal;
        // Evaluate prior signals, so cycles have a defined one-tick delay.
        nextSignal[i]=powered[i]&&logic(g.operation,av,bv);
    }
    if(!world.prepareExistingWrite<Node>())return false;
    for(std::size_t i=0;i<nodes.size();++i){
        auto& n=nodes[i].second;
        n.powered=powered[i];n.signal=nextSignal[i];n.sampledTick=tick;
        world.get<Node>(nodes[i].first)=n;
    }
    if(activity)*activity=stats;
    return true;
}
inline bool advance(World& world,std::uint32_t tick){
    PowerCache cache;
    return advance(world,tick,cache);
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Node>("circuit.node",1);
    sim.registerPersistentComponent<Wire>("circuit.wire",1);
    sim.registerPersistentComponent<Gate>("circuit.gate",1);
}
inline void install(Simulation& sim,std::string name="circuit.power_logic"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[cache=PowerCache{}](Simulation& s) mutable {(void)advance(s.world(),s.tick(),cache);});
}
} // namespace aiverse::circuit
