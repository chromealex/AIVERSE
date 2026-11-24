#pragma once
// Stage45 shared ECS navigation-to-locomotion contract.
// AIVERSE's movement controllers (ground, flying, swimming, vehicles,
// user-defined) consume an ECS steering request. This module NEVER writes
// physical position/velocity, owns no motor and imports no actor module.
// Systems remain genre-neutral and use one committed cave SDF epoch.
#include "engine/nav/CaveSimulationNavigation.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>

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
using CavePositionSource=std::function<
    terraweave::caverns::PointCm(Entity)>;

// An owning motor sets its own position component after it has checked
// actual physics contacts. This system only converts waypoints into
// a common read-only-ish ECS desired-motion command.
inline size_t prepareCaveSteering(
    World& ecs,
    const terraweave::caverns::CaveNavigationDataProvider& published,
    const CavePositionSource& authoritativePosition,
    uint32_t maxEntities=65536) {
    using namespace terraweave::caverns;
    if(!authoritativePosition||!maxEntities)
        throw std::invalid_argument("Caverns missing motor position source");
    size_t count=0;
    ecs.each<CaveSteeringRequest,CaveRouteFollower>(
        [&](Entity entity,CaveSteeringRequest& out,
            CaveRouteFollower& route){
        if(count++>=maxEntities)return;
        out.valid=false;
        out.sourceRevision=published.revision();
        out.environmentRevision=published.environmentRevision();
        if(route.needsRepath||route.arrived||route.current>=route.waypoints.size()||
           route.sourceRevision!=published.revision()||
           route.environmentRevision!=published.environmentRevision()){
            route.needsRepath=route.sourceRevision!=published.revision()||
                route.environmentRevision!=published.environmentRevision()||
                route.needsRepath;
            return;
        }
        const auto start=authoritativePosition(entity);
        while(route.current<route.waypoints.size()) {
            const auto end=route.waypoints[route.current];
            const int64_t dx=end.x-start.x,dy=end.y-start.y,
                          dz=end.z-start.z;
            const int64_t threshold=std::max<int64_t>(1,route.agent.sampleCm);
            if(std::max({std::abs(dx),std::abs(dy),std::abs(dz)})>threshold)
                break;
            ++route.current;
        }
        if(route.current>=route.waypoints.size()){
            route.arrived=true;return;
        }
        const auto goal=route.waypoints[route.current];
        // The controller may have a different clearance shape; before
        // emitting the request, still verify baseline SDF occupancy.
        if(!published.canTraverse(start,goal,route.agent)){
            route.needsRepath=true;return;
        }
        const auto clamp=[](int64_t v)->int32_t{
            return int32_t(std::clamp<int64_t>(v,INT32_MIN,INT32_MAX));
        };
        out.destination=goal;
        out.dxCm=clamp(goal.x-start.x);
        out.dyCm=clamp(goal.y-start.y);
        out.dzCm=clamp(goal.z-start.z);
        out.traversal=route.agent.traversal;
        out.maxSpeedCmPerTick=route.speedCmPerTick;
        out.valid=true;
    });
    return std::min<size_t>(count,maxEntities);
}
// Separate from the old Transform-only mover. Each game installs this
// before its own movement controller, which alone changes physical state.
inline void installCaveSteeringSystem(
    Simulation& sim,CavePublishedNavSource published,
    CavePositionSource authoritativePosition,
    uint32_t maxEntities=65536) {
    if(!published||!authoritativePosition||!maxEntities)
        throw std::invalid_argument("Caverns steering install");
    sim.registerPersistentComponent<CaveSteeringRequest>(
        "caverns.steering.request",1);
    sim.addSystem("caverns.navigation.intent",
        [published,authoritativePosition,maxEntities](Simulation& state){
            const auto source=published();
            if(source)(void)prepareCaveSteering(
                state.world(),*source,authoritativePosition,maxEntities);
        });
}
// Stable endian-independent schema: never store pointers to controllers.
inline void io(Archive& a,CaveSteeringRequest& r){
    a(r.destination,r.dxCm,r.dyCm,r.dzCm,r.maxSpeedCmPerTick,
      r.traversal,r.sourceRevision,r.environmentRevision,r.valid);
}
} // namespace aiverse::nav
