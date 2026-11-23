// CROWD authoritative route intent and waypoint cursor.
// The existing engine/nav/NavMesh.h computes paths. CROWD consumes only
// quantized waypoints and does not own, duplicate or rebuild NavMesh.
#pragma once
#include "CrowdEcs.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::crowd {
inline constexpr std::size_t kMaxRoutePoints=128;
struct Waypoint { I x=0,z=0; };
struct Route {
    std::vector<Waypoint> points;
    std::uint16_t cursor=0;
    std::uint32_t navigationRevision=0;
    std::uint32_t sampledTick=UINT32_MAX;
    I arrivalRadius=20;
    bool active=false;
};
inline bool valid(const Waypoint& p){
    return p.x>=-kCoord&&p.x<=kCoord&&p.z>=-kCoord&&p.z<=kCoord;
}
inline bool valid(const Route& r){
    if(r.points.size()>kMaxRoutePoints||r.cursor>r.points.size()||
       r.arrivalRadius<0||r.arrivalRadius>kMaxRadius)return false;
    for(const auto& p:r.points)if(!valid(p))return false;
    return !r.active||r.cursor<r.points.size();
}
inline void io(Archive& archive,Waypoint& p){archive(p.x,p.z);}
inline void io(Archive& archive,Route& r){
    archive(r.points,r.cursor,r.navigationRevision,r.sampledTick,
            r.arrivalRadius,r.active);
    if(!archive.writing()&&!valid(r))archive.fail();
}
// Install a full newly validated route on one live CROWD Agent. The native
// host calls this on the World owner thread during its command barrier.
inline bool assignRoute(World& world,Entity e,
                        std::vector<Waypoint> waypoints,
                        std::uint32_t revision,I arrivalRadius=20){
    if(!world.alive(e)||!world.has<Agent>(e)||
       waypoints.empty()||waypoints.size()>kMaxRoutePoints||
       arrivalRadius<0||arrivalRadius>kMaxRadius)return false;
    for(const auto& p:waypoints)if(!valid(p))return false;
    Route r;
    r.points=std::move(waypoints);
    r.navigationRevision=revision;
    r.arrivalRadius=arrivalRadius;
    r.active=true;
    // A changed navmesh source invalidates the route, not the CROWD pool.
    if(world.has<Route>(e))world.get<Route>(e)=std::move(r);
    else world.add<Route>(e,std::move(r));
    return true;
}
// Routed agents expose the next waypoint to CROWD's existing movement/slot
// scheduler. Route progression is snapshot-isolated and generation-safe.
// Call this BEFORE crowd::advance on every fixed simulation tick.
inline bool advanceRoutes(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Route>()>kMaxAgents)return false;
    std::vector<std::pair<Entity,Route>> routes;
    read.eachExistingConst<Route>([&](Entity e,const Route& r){
        if(read.alive(e))routes.push_back({e,r});
    });
    std::sort(routes.begin(),routes.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    struct AgentGoal {Entity e;I x,z;};
    std::vector<AgentGoal> goals;
    for(auto& [e,r]:routes){
        if(!valid(r))return false;
        if(r.sampledTick==tick)continue;
        const auto* a=read.tryGetConst<Agent>(e);
        if(!a||!valid(*a)||!a->enabled){
            r.sampledTick=tick;continue;
        }
        if(r.active){
            while(r.cursor<r.points.size()){
                const auto p=r.points[r.cursor];
                const I dx=p.x-a->x,dz=p.z-a->z;
                // All positions bounded to ±1e6, squares fit int64.
                if(dx*dx+dz*dz>r.arrivalRadius*r.arrivalRadius)break;
                ++r.cursor;
            }
            if(r.cursor>=r.points.size())r.active=false;
            else goals.push_back({e,r.points[r.cursor].x,r.points[r.cursor].z});
        }
        r.sampledTick=tick;
    }
    if(routes.empty())return true;
    if(!world.prepareExistingWrite<Route>())return false;
    if(!goals.empty()&&!world.prepareExistingWrite<Agent>())return false;
    for(const auto& [e,r]:routes)world.get<Route>(e)=r;
    for(const auto& g:goals){
        auto& agent=world.get<Agent>(g.e);
        agent.goalX=g.x;agent.goalZ=g.z;
    }
    return true;
}
inline void installRoutes(Simulation& sim,
                          std::string name="crowd.nav_routes"){
    sim.registerPersistentComponent<Route>("crowd.route",1);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)advanceRoutes(s.world(),s.tick());
    });
}
} // namespace aiverse::crowd
