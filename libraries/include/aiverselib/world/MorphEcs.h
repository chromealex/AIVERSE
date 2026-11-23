// MORPH: deterministic deformable constraint graph, not an alternative Box3D.
// Quantized displacements are owned by ECS; render meshes/physics consume them.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::morph {
using I=std::int64_t;
inline constexpr I kPositionLimit=1000000,kForceLimit=1000000;
inline constexpr std::size_t kMaxNodes=4096,kMaxSprings=16384,kMaxForces=8192;
// Position and rest are quantized relative to the owning rigid body's local frame.
struct Node {
    I x=0,y=0,z=0,vx=0,vy=0,vz=0;
    I restX=0,restY=0,restZ=0;
    I drag=100; // per-mille velocity damping, [0..1000]
    bool anchored=false,enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Spring {
    Entity a=kNullEntity,b=kNullEntity;
    I restDx=0,restDy=0,restDz=0; // target b-a offset
    I stiffness=400;              // [0..1000] correction/tick
    bool enabled=true;
};
struct Force {
    Entity node=kNullEntity;
    I x=0,y=0,z=0; // per-tick authored impulse, not a physics force
    bool enabled=true;
};
// One-shot host contact impulse. Unlike Force it is consumed exactly once
// on its scheduled tick, including under COW rollback and save/replay.
// The authoritative host quantizes external Box3D input before enqueueing.
struct Impact {
    I x=0,y=0,z=0;
    std::uint32_t applyTick=UINT32_MAX;
    bool pending=false;
};
inline void io(Archive& a,Node& n){
    a(n.x,n.y,n.z,n.vx,n.vy,n.vz,n.restX,n.restY,n.restZ,
      n.drag,n.anchored,n.enabled,n.sampledTick);
    if(!a.writing() &&
       (n.drag<0||n.drag>1000||
        n.x< -kPositionLimit||n.x>kPositionLimit||
        n.y< -kPositionLimit||n.y>kPositionLimit||
        n.z< -kPositionLimit||n.z>kPositionLimit||
        n.vx< -kForceLimit||n.vx>kForceLimit||
        n.vy< -kForceLimit||n.vy>kForceLimit||
        n.vz< -kForceLimit||n.vz>kForceLimit||
        n.restX< -kPositionLimit||n.restX>kPositionLimit||
        n.restY< -kPositionLimit||n.restY>kPositionLimit||
        n.restZ< -kPositionLimit||n.restZ>kPositionLimit))a.fail();
}
inline void io(Archive& a,Spring& s){
    a(s.a,s.b,s.restDx,s.restDy,s.restDz,s.stiffness,s.enabled);
    if(!a.writing()&&(s.stiffness<0||s.stiffness>1000||
        s.restDx< -kPositionLimit||s.restDx>kPositionLimit||
        s.restDy< -kPositionLimit||s.restDy>kPositionLimit||
        s.restDz< -kPositionLimit||s.restDz>kPositionLimit))a.fail();
}
inline void io(Archive& a,Force& f){
    a(f.node,f.x,f.y,f.z,f.enabled);
    if(!a.writing()&&(f.x< -kForceLimit||f.x>kForceLimit||
        f.y< -kForceLimit||f.y>kForceLimit||
        f.z< -kForceLimit||f.z>kForceLimit))a.fail();
}
inline bool valid(const Impact& i){
    return i.x>=-kForceLimit&&i.x<=kForceLimit&&
           i.y>=-kForceLimit&&i.y<=kForceLimit&&
           i.z>=-kForceLimit&&i.z<=kForceLimit;
}
inline void io(Archive& a,Impact& i){
    a(i.x,i.y,i.z,i.applyTick,i.pending);
    if(!a.writing()&&!valid(i))a.fail();
}
// Enqueue on the ECS owner thread after a physics contact barrier.
// Multiple contacts on a node/tick are merged with saturation, not lost.
// Callers must supply only validated, authoritative replayable inputs.
inline bool enqueueImpact(World& world,Entity node,std::uint32_t tick,
                          I dx,I dy,I dz){
    if(!world.alive(node)||!world.has<Node>(node)||
       !valid(Impact{dx,dy,dz,tick,true}))return false;
    Impact next;
    if(const auto* previous=static_cast<const World&>(world).tryGetConst<Impact>(node))
        next=*previous;
    if(next.pending && next.applyTick!=tick)return false;
    if(!next.pending){
        next={0,0,0,tick,true};
    }
    next.x=std::clamp(next.x+dx,-kForceLimit,kForceLimit);
    next.y=std::clamp(next.y+dy,-kForceLimit,kForceLimit);
    next.z=std::clamp(next.z+dz,-kForceLimit,kForceLimit);
    if(!world.has<Impact>(node))world.add<Impact>(node,next);
    else world.get<Impact>(node)=next;
    return true;
}
inline I limit(I n,I bound){return std::clamp(n,-bound,bound);}
inline bool valid(const Node& n){
    return n.drag>=0&&n.drag<=1000&&
           n.x>=-kPositionLimit&&n.x<=kPositionLimit&&
           n.y>=-kPositionLimit&&n.y<=kPositionLimit&&
           n.z>=-kPositionLimit&&n.z<=kPositionLimit&&
           n.restX>=-kPositionLimit&&n.restX<=kPositionLimit&&
           n.restY>=-kPositionLimit&&n.restY<=kPositionLimit&&
           n.restZ>=-kPositionLimit&&n.restZ<=kPositionLimit&&
           n.vx>=-kForceLimit&&n.vx<=kForceLimit&&
           n.vy>=-kForceLimit&&n.vy<=kForceLimit&&
           n.vz>=-kForceLimit&&n.vz<=kForceLimit;
}
inline bool valid(const Spring& s){
    return s.stiffness>=0&&s.stiffness<=1000&&
           s.restDx>=-kPositionLimit&&s.restDx<=kPositionLimit&&
           s.restDy>=-kPositionLimit&&s.restDy<=kPositionLimit&&
           s.restDz>=-kPositionLimit&&s.restDz<=kPositionLimit;
}
inline bool valid(const Force& f){
    return f.x>=-kForceLimit&&f.x<=kForceLimit&&
           f.y>=-kForceLimit&&f.y<=kForceLimit&&
           f.z>=-kForceLimit&&f.z<=kForceLimit;
}
// All calculations are staged. Writes to the native pools occur only on success.
// Graph edges hold full Entity generations; a stale link never rejoins a new node.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Node>()>kMaxNodes||
       read.countExisting<Spring>()>kMaxSprings||
       read.countExisting<Force>()>kMaxForces||
       read.countExisting<Impact>()>kMaxNodes)return false;
    struct Item{Entity id;Node value;};
    std::vector<Item> nodes;
    read.eachExistingConst<Node>([&](Entity e,const Node& n){
        if(read.alive(e))nodes.push_back({e,n});
    });
    std::sort(nodes.begin(),nodes.end(),[](auto& a,auto& b){return a.id.packed()<b.id.packed();});
    if(nodes.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<nodes.size();++i){
        if(!valid(nodes[i].value))return false;
        ids.emplace(nodes[i].id.packed(),i);
        all &= nodes[i].value.sampledTick==tick;
        any |= nodes[i].value.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<std::pair<Entity,Spring>> springs;
    read.eachExistingConst<Spring>([&](Entity e,const Spring& s){
        if(read.alive(e)&&s.enabled)springs.push_back({e,s});
    });
    std::sort(springs.begin(),springs.end(),
        [](auto& a,auto& b){return a.first.packed()<b.first.packed();});
    std::vector<std::pair<Entity,Force>> forces;
    read.eachExistingConst<Force>([&](Entity e,const Force& f){
        if(read.alive(e)&&f.enabled)forces.push_back({e,f});
    });
    std::sort(forces.begin(),forces.end(),
        [](auto& a,auto& b){return a.first.packed()<b.first.packed();});
    std::vector<std::array<I,3>> acceleration(nodes.size());
    for(const auto& [e,s]:springs){
        (void)e;
        if(!valid(s))return false;
        const auto a=ids.find(s.a.packed()),b=ids.find(s.b.packed());
        if(a==ids.end()||b==ids.end()||a==b)continue;
        const auto& n=nodes[a->second].value;
        const auto& m=nodes[b->second].value;
        const I diff[3]={m.x-n.x-s.restDx,m.y-n.y-s.restDy,m.z-n.z-s.restDz};
        for(int axis=0;axis<3;++axis){
            // Bound positions to 1e6, rest offset to 1e6, stiffness<=1000:
            // each scaled correction <=3e6 and cannot overflow int64.
            const I delta=diff[axis]*s.stiffness/1000;
            auto& aa=acceleration[a->second][std::size_t(axis)];
            auto& bb=acceleration[b->second][std::size_t(axis)];
            aa=limit(aa+limit(delta,kForceLimit),kForceLimit);
            bb=limit(bb-limit(delta,kForceLimit),kForceLimit);
        }
    }
    for(const auto& [e,f]:forces){
        (void)e;
        if(!valid(f))return false;
        const auto it=ids.find(f.node.packed());
        if(it==ids.end())continue;
        const I v[3]={f.x,f.y,f.z};
        for(int axis=0;axis<3;++axis){
            auto& a=acceleration[it->second][std::size_t(axis)];
            a=limit(a+v[axis],kForceLimit);
        }
    }
    // Apply scheduled one-shot contacts after continuous spring/Force terms.
    // Stage their acknowledgement; no authoritative pool is mutated until
    // all Nodes and pending Impact values have been validated.
    std::vector<std::pair<Entity,Impact>> consumed;
    read.eachExistingConst<Impact>([&](Entity e,const Impact& impulse){
        if(read.alive(e))consumed.push_back({e,impulse});
    });
    std::sort(consumed.begin(),consumed.end(),
        [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(auto& [e,impulse]:consumed){
        if(!valid(impulse))return false;
        if(!impulse.pending)continue;
        if(impulse.applyTick!=tick)return false;
        const auto it=ids.find(e.packed());
        if(it==ids.end())return false;
        I delta[3]{impulse.x,impulse.y,impulse.z};
        for(int axis=0;axis<3;++axis){
            auto& out=acceleration[it->second][std::size_t(axis)];
            out=limit(out+delta[axis],kForceLimit);
        }
        impulse.pending=false;
        impulse.x=impulse.y=impulse.z=0;
    }
    for(std::size_t i=0;i<nodes.size();++i){
        auto& n=nodes[i].value;
        if(!n.enabled){n.sampledTick=tick;continue;}
        if(n.anchored){
            n.x=n.restX;n.y=n.restY;n.z=n.restZ;
            n.vx=n.vy=n.vz=0;n.sampledTick=tick;continue;
        }
        I position[3]={n.x,n.y,n.z},vel[3]={n.vx,n.vy,n.vz};
        const I rest[3]={n.restX,n.restY,n.restZ};
        for(int j=0;j<3;++j){
            // Local shape-memory correction uses the original MORPH policy.
            I desired=position[j];
            if(!lib::worldprocess::relax(desired,rest[j],250))return false;
            I inertia=limit(vel[j]+acceleration[i][std::size_t(j)],kForceLimit);
            inertia=inertia*(1000-n.drag)/1000;
            vel[j]=limit(inertia,kForceLimit);
            position[j]=limit(desired+vel[j],kPositionLimit);
        }
        n.x=position[0];n.y=position[1];n.z=position[2];
        n.vx=vel[0];n.vy=vel[1];n.vz=vel[2];
        n.sampledTick=tick;
    }
    if(!world.prepareExistingWrite<Node>())return false;
    if(!consumed.empty()&&!world.prepareExistingWrite<Impact>())return false;
    for(const auto& item:nodes)world.get<Node>(item.id)=item.value;
    for(const auto& [e,impact]:consumed)world.get<Impact>(e)=impact;
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Node>("morph.node",1);
    sim.registerPersistentComponent<Spring>("morph.spring",1);
    sim.registerPersistentComponent<Force>("morph.force",1);
    sim.registerPersistentComponent<Impact>("morph.impact",1);
}
inline void install(Simulation& sim,std::string name="morph.constraints"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::morph
