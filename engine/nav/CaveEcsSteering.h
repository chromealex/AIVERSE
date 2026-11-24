#pragma once
// Shared ECS navigation-to-locomotion contract: this code NEVER writes
// physical position or velocity, owns no motor and imports no actor module.
#include "engine/nav/CaveSimulationNavigation.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::nav {
struct CaveSteeringRequest {
    terraweave::caverns::PointCm destination{};
    int32_t dxCm=0,dyCm=0,dzCm=0;
    uint32_t maxSpeedCmPerTick=0;
    terraweave::caverns::CaveTraversal traversal=
        terraweave::caverns::CaveTraversal::Ground;
    uint64_t sourceRevision=0,environmentRevision=0;
    bool valid=false;
};
// The legacy single-argument callback is retained as a thin adapter only.
// New hosts use CaveWorldPositionSource to read the current/restored World.
using CavePositionSource=std::function<terraweave::caverns::PointCm(Entity)>;
inline CaveWorldPositionSource adaptCavePositionSource(CavePositionSource position){
    if(!position)return {};
    return [position=std::move(position)](const World&,Entity e)
        ->std::optional<terraweave::caverns::PointCm>{return position(e);};
}
inline void io(Archive& a,CaveSteeringRequest& r){
    a(r.destination,r.dxCm,r.dyCm,r.dzCm,r.maxSpeedCmPerTick,
      r.traversal,r.sourceRevision,r.environmentRevision,r.valid);
}
// Call on an already authorized gameplay command/tick, not directly from
// client/network/UI. Route assignment never creates a second mover.
inline bool assignCaveMotorGoal(
    World& ecs,Entity e,terraweave::caverns::PointCm goal,
    terraweave::caverns::CaveAgentProfile profile,
    std::string algorithm="astar",uint32_t speedCmPerTick=25,
    uint32_t maxVisits=8192) {
    if(!assignCaveGoal(ecs,e,goal,profile,std::move(algorithm),
                       speedCmPerTick,maxVisits))return false;
    if(auto* request=ecs.tryGet<CaveSteeringRequest>(e))*request={};
    else ecs.add<CaveSteeringRequest>(e,CaveSteeringRequest{});
    return true;
}
inline bool cancelCaveMotorGoal(World& ecs,Entity e) {
    auto* intent=ecs.tryGet<CaveRouteIntent>(e);
    if(!intent)return false;
    intent->active=false;
    if(auto* out=ecs.tryGet<CaveSteeringRequest>(e))*out={};
    return true;
}
// The motor may report actual contact/blockage at the fixed tick. It
// retains exclusive ownership of the physical response. Invalidate any
// external dynamic-obstacle route cache when applicable.
inline bool reportCaveMotorBlocked(World& ecs,Entity e,
    uint64_t sourceRevision,uint64_t environmentRevision){
    auto* route=ecs.tryGet<CaveRouteFollower>(e);
    auto* out=ecs.tryGet<CaveSteeringRequest>(e);
    if(!route||!out||!out->valid||
       route->sourceRevision!=sourceRevision||
       route->environmentRevision!=environmentRevision||
       out->sourceRevision!=sourceRevision||
       out->environmentRevision!=environmentRevision)return false;
    route->needsRepath=true;
    *out={};
    return true;
}
inline void clearCaveSteering(World& ecs) {
    ecs.eachExisting<CaveSteeringRequest>(
        [](Entity,CaveSteeringRequest& request){request={};});
}
// Read from the current ECS world only after this tick's producer ran.
// Refuse stale revisions, missing routes, canceled goals or blocked motors.
inline const CaveSteeringRequest* caveMotorRequest(
    const World& ecs,Entity e,
    const terraweave::caverns::CaveNavigationDataProvider& published) {
    const auto* request=ecs.tryGetConst<CaveSteeringRequest>(e);
    const auto* route=ecs.tryGetConst<CaveRouteFollower>(e);
    const auto* intent=ecs.tryGetConst<CaveRouteIntent>(e);
    if(!request||!request->valid||!route||route->needsRepath||route->arrived||
       (intent&&!intent->active)||
       request->sourceRevision!=published.revision()||
       request->environmentRevision!=published.environmentRevision()||
       route->sourceRevision!=request->sourceRevision||
       route->environmentRevision!=request->environmentRevision)return nullptr;
    return request;
}
// Clear all previous requests, even those outside the current processing
// budget or no longer carrying a route. Fixed-tick rotation avoids starving
// the tail of the component pool. The existing motor must treat no valid
// request as no cave steering. No cached component pointers across ticks.
inline size_t prepareCaveSteering(
    World& ecs,
    const terraweave::caverns::CaveNavigationDataProvider& published,
    const CaveWorldPositionSource& authoritativePosition,
    uint32_t maxEntities=65536,uint64_t rotation=0) {
    using namespace terraweave::caverns;
    if(!authoritativePosition||!maxEntities||maxEntities>65536)
        throw std::invalid_argument("Caverns missing motor position source/budget");
    clearCaveSteering(ecs);
    const size_t total=ecs.countExisting<CaveSteeringRequest>();
    if(!total)return 0;
    const size_t first=size_t(rotation%total);
    const size_t budget=std::min<size_t>(total,maxEntities);
    const auto emit=[&](Entity entity,CaveSteeringRequest& out){
        auto* ptr=ecs.tryGet<CaveRouteFollower>(entity);
        const auto* intent=ecs.tryGetConst<CaveRouteIntent>(entity);
        if(!ptr||(intent&&!intent->active))return;
        auto& route=*ptr;
        if(route.sourceRevision!=published.revision()||
           route.environmentRevision!=published.environmentRevision()){
            route.needsRepath=true;return;
        }
        if(route.needsRepath||route.arrived||!route.speedCmPerTick||
           route.speedCmPerTick>10000)return;
        if(route.current>=route.waypoints.size()){
            route.arrived=true;return;
        }
        const auto position=authoritativePosition(ecs,entity);
        if(!position||!validCaveRoutePoint(*position)){
            route.needsRepath=true;return;
        }
        const auto start=*position;
        while(route.current<route.waypoints.size()) {
            const auto end=route.waypoints[route.current];
            if(!validCaveRoutePoint(end)){route.needsRepath=true;return;}
            const int64_t dx=end.x-start.x,dy=end.y-start.y,dz=end.z-start.z;
            const int64_t threshold=std::max<int64_t>(1,route.agent.sampleCm);
            if(std::max({std::abs(dx),std::abs(dy),std::abs(dz)})>threshold||
               !published.canTraverse(start,end,route.agent))break;
            ++route.current;
        }
        if(route.current>=route.waypoints.size()){
            route.arrived=true;return;
        }
        const auto goal=route.waypoints[route.current];
        if(!validCaveRoutePoint(goal)||!published.canTraverse(start,goal,route.agent)){
            route.needsRepath=true;return;
        }
        out.destination=goal;
        // World coordinates are already bounded to +/- 1e9 cm.
        out.dxCm=int32_t(goal.x-start.x);
        out.dyCm=int32_t(goal.y-start.y);
        out.dzCm=int32_t(goal.z-start.z);
        out.traversal=route.agent.traversal;
        out.maxSpeedCmPerTick=route.speedCmPerTick;
        out.sourceRevision=published.revision();
        out.environmentRevision=published.environmentRevision();
        out.valid=true;
    };
    const size_t tail=std::min(budget,total-first);
    ecs.eachExistingRange<CaveSteeringRequest>(first,first+tail,emit);
    if(tail<budget)ecs.eachExistingRange<CaveSteeringRequest>(0,budget-tail,emit);
    return budget;
}
inline size_t prepareCaveSteering(
    World& ecs,
    const terraweave::caverns::CaveNavigationDataProvider& published,
    const CavePositionSource& position,uint32_t maxEntities=65536) {
    return prepareCaveSteering(ecs,published,
        adaptCavePositionSource(position),maxEntities);
}
inline void installCaveSteeringSystem(
    Simulation& sim,CavePublishedNavSource published,
    CaveWorldPositionSource position,uint32_t maxEntities=65536,
    std::optional<size_t> beforeSystem=std::nullopt) {
    if(!published||!position||!maxEntities||maxEntities>65536)
        throw std::invalid_argument("Caverns steering install");
    sim.registerPersistentComponent<CaveSteeringRequest>(
        "caverns.steering.request",1);
    Simulation::SystemFn tick=[published,position,maxEntities](Simulation& state){
        const auto source=published();
        if(source)(void)prepareCaveSteering(
            state.world(),*source,position,maxEntities,
            uint64_t(state.tick())*maxEntities);
        else clearCaveSteering(state.world());
    };
    if(beforeSystem)sim.insertSystem(*beforeSystem,"caverns.navigation.intent",std::move(tick));
    else sim.addSystem("caverns.navigation.intent",std::move(tick));
}
inline void installCaveSteeringSystem(
    Simulation& sim,CavePublishedNavSource published,
    CavePositionSource position,uint32_t maxEntities=65536) {
    installCaveSteeringSystem(sim,std::move(published),
        adaptCavePositionSource(std::move(position)),maxEntities);
}
// Preferred host setup: attach exactly once, AFTER the existing motor
// has been registered. Insert repath -> steering immediately BEFORE it.
// No Transform mover, actor dependency or implicit motor implementation.
inline void installCaveMotorNavigationBefore(
    Simulation& sim,CaveSimulationNavigationHooks hooks,
    CaveWorldPositionSource position,const std::string& motorSystem,
    uint32_t maxEntities=65536) {
    if(!hooks.publishedSource||!hooks.search||!position||
       !hooks.maxRepairsPerTick||hooks.maxRepairsPerTick>65536||
       !maxEntities||maxEntities>65536||motorSystem.empty())
        throw std::invalid_argument("Caverns motor navigation setup");
    std::optional<size_t> index;
    for(size_t i=0;i<sim.systemCount();++i){
        const auto& name=sim.systemName(i);
        if(name=="caverns.navigation.repath"||
           name=="caverns.navigation.intent"||
           name=="caverns.navigation.follow")
            throw std::logic_error("Caverns navigation already installed");
        if(name==motorSystem){
            if(index)throw std::invalid_argument("Caverns ambiguous motor system");
            index=i;
        }
    }
    if(!index)throw std::invalid_argument("Caverns motor system not found");
    hooks.position=position;
    const auto source=hooks.publishedSource;
    installCaveRepathSimulation(sim,std::move(hooks),*index);
    installCaveSteeringSystem(sim,source,std::move(position),maxEntities,*index+1);
}
} // namespace aiverse::nav
