#pragma once
// Shared engine Stage43: opt-in deterministic Caverns ECS navigation systems.
// The REAL Simulation owner calls install once at setup, supplying a published
// immutable SDF source, a route solver and an authoritative collision owner.
// Never install from visual-only GameClient or relay net::Server.
#include "engine/nav/CaveEcsRouteFollow.h"
#include "engine/sim/Simulation.h"
#include "engine/core/Archive.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::terraweave::caverns {
inline void io(::aiverse::Archive& a,PointCm& p){
    a(p.x,p.y,p.z);
}
inline void io(::aiverse::Archive& a,CaveAgentProfile& p){
    a(p.traversal,p.radiusCm,p.heightCm,p.maxStepCm,
      p.groundProbeCm,p.sampleCm,p.areaMask);
}
} // namespace aiverse::terraweave::caverns

namespace aiverse::nav {
struct CaveRouteIntent {
    terraweave::caverns::PointCm goal{};
    std::string algorithm="astar";
    uint32_t repathNotBeforeTick=0;
    uint32_t attempts=0;
    uint32_t retryTicks=10;
    uint32_t maxVisits=8192;
    bool active=true;
};
struct CaveRepathTickStats {
    uint32_t attempted=0,assigned=0,failed=0,unavailable=0;
};
using CavePublishedNavSource=std::function<
    std::shared_ptr<const terraweave::caverns::CaveNavigationDataProvider>()>;
using CaveRouteSearch=std::function<terraweave::caverns::CavePathResult(
    const std::string&,const terraweave::caverns::CavePathQuery&)>;
struct CaveSimulationNavigationHooks {
    CavePublishedNavSource publishedSource;
    CaveRouteSearch search;
    CaveMovementCollision collision;
    std::function<void()> invalidateDerived;
    uint32_t maxRepairsPerTick=8;
    uint32_t maxMovedPerTick=65536;
};
inline void io(Archive& a,CaveRouteFollower& p) {
    // Serialize bounded route points; avoid a corrupted/untrusted save
    // allocating an unbounded vector from a bogus length field.
    uint32_t n=a.count(p.waypoints.size());
    if(n>65536){a.fail();return;}
    if(!a.writing())p.waypoints.resize(n);
    for(auto& waypoint:p.waypoints)io(a,waypoint);
    io(a,p.agent);
    a(p.sourceRevision,p.environmentRevision,p.current,p.speedCmPerTick,
      p.arrived,p.needsRepath);
    if(!a.writing()&&p.current>p.waypoints.size())a.fail();
}
inline void io(Archive& a,CaveRouteIntent& p){
    io(a,p.goal);
    a(p.algorithm,p.repathNotBeforeTick,p.attempts,
      p.retryTicks,p.maxVisits,p.active);
    if(!a.writing() &&
       (p.algorithm.size()>128||p.retryTicks>100000||
        !p.maxVisits||p.maxVisits>1048576))a.fail();
}
// Submit a route destination into authoritative ECS state. Commands may
// request any registered solver, including custom. No route is calculated
// on the presentation/UI thread. The fixed-tick repath system resolves it.
inline bool assignCaveGoal(
    World& world,Entity entity,
    terraweave::caverns::PointCm goal,
    terraweave::caverns::CaveAgentProfile profile,
    std::string algorithm="astar",uint32_t speedCmPerTick=25,
    uint32_t maxVisits=8192) {
    if(!world.alive(entity)||!world.has<Transform>(entity)||
       algorithm.empty()||algorithm.size()>128||
       !speedCmPerTick||speedCmPerTick>10000||
       !maxVisits||maxVisits>1048576)
        return false;
    terraweave::caverns::validatePoint(goal);
    terraweave::caverns::CaveNavigationDataProvider::validateProfile(profile);
    CaveRouteIntent intent;
    intent.goal=goal;intent.algorithm=std::move(algorithm);
    intent.maxVisits=maxVisits;intent.repathNotBeforeTick=0;
    CaveRouteFollower route;
    route.agent=profile;route.speedCmPerTick=speedCmPerTick;
    route.needsRepath=true;
    if(world.has<CaveRouteIntent>(entity))
        world.get<CaveRouteIntent>(entity)=std::move(intent);
    else world.add<CaveRouteIntent>(entity,std::move(intent));
    if(world.has<CaveRouteFollower>(entity))
        world.get<CaveRouteFollower>(entity)=std::move(route);
    else world.add<CaveRouteFollower>(entity,std::move(route));
    return true;
}
// Functions below write to existing shared ECS components only; no separate
// agent container, floating point, wall-clock time or random scheduling.
inline CaveRepathTickStats repathCaveEntities(
    Simulation& sim,
    const CaveSimulationNavigationHooks& hooks) {
    CaveRepathTickStats stats;
    if(!hooks.publishedSource||!hooks.search||!hooks.collision)return stats;
    const auto provider=hooks.publishedSource();
    if(!provider||!provider->revision())return stats;
    World& world=sim.world();
    const uint32_t tick=sim.tick();
    world.each<CaveRouteIntent,CaveRouteFollower,Transform>(
        [&](Entity entity,CaveRouteIntent& intent,
            CaveRouteFollower& route,Transform& transform){
        if(!intent.active||stats.attempted>=hooks.maxRepairsPerTick)
            return;
        // Force a stale route to be re-planned in a bounded stable order.
        if(route.sourceRevision!=provider->revision() ||
           route.environmentRevision!=provider->environmentRevision()){
            if(!route.needsRepath)
                intent.repathNotBeforeTick=tick;
            route.needsRepath=true;
        }
        if(!route.needsRepath && !route.waypoints.empty() &&
           !route.arrived)return;
        if(!route.needsRepath && route.arrived)return;
        if(tick<intent.repathNotBeforeTick)return;
        ++stats.attempted;
        terraweave::caverns::CavePathQuery request;
        request.start=caveTransformCm(transform);
        request.goal=intent.goal;
        request.agent=route.agent;
        request.sourceRevision=provider->revision();
        request.maxVisits=intent.maxVisits;
        auto result=hooks.search(intent.algorithm,request);
        if(!result.reached||result.sourceRevision!=request.sourceRevision||
           result.waypoints.empty()){
            ++stats.failed;
            if(intent.attempts<UINT32_MAX)++intent.attempts;
            const uint32_t delay=std::max(1u,intent.retryTicks);
            intent.repathNotBeforeTick=tick>UINT32_MAX-delay?
                UINT32_MAX:tick+delay;
            route.needsRepath=true;
            return;
        }
        const auto profile=route.agent;
        const auto speed=route.speedCmPerTick;
        // assignment replaces the old route in-place without touching
        // an unrelated ECS/actor container.
        if(!assignCaveRoute(world,entity,result,profile,speed)){
            ++stats.unavailable;return;
        }
        intent.attempts=0;
        intent.repathNotBeforeTick=tick;
        ++stats.assigned;
    });
    return stats;
}
inline void installCaveNavigationSimulation(
    Simulation& sim,CaveSimulationNavigationHooks hooks){
    if(!hooks.publishedSource||!hooks.search||!hooks.collision ||
       !hooks.maxRepairsPerTick||hooks.maxRepairsPerTick>65536||
       !hooks.maxMovedPerTick||hooks.maxMovedPerTick>65536)
        throw std::invalid_argument("Caverns Simulation requires native nav/physics owners");
    // This is opt-in in the actual game/authority setup, not GameCore's
    // unconditional rule for unrelated 2D or visual-only projects.
    sim.registerPersistentComponent<CaveRouteFollower>("caverns.route",2);
    sim.registerPersistentComponent<CaveRouteIntent>("caverns.route.intent",1);
    sim.addSystem("caverns.navigation.repath",
        [hooks](Simulation& state){
            (void)repathCaveEntities(state,hooks);
        });
    sim.addSystem("caverns.navigation.follow",
        [hooks](Simulation& state){
            const auto source=hooks.publishedSource();
            if(!source)return;
            const auto result=updateCaveRouteFollowers(
                state.world(),*source,hooks.collision,hooks.maxMovedPerTick);
            // A native body may have appeared while SDF is unchanged.
            // Invalidate derived route memoization before the next repath.
            if(result.blocked&&hooks.invalidateDerived)
                hooks.invalidateDerived();
        });
    // Rollback restores authoritative route components, but derived
    // solver caches may belong to a newer frame. Mark repath explicitly.
    sim.addStateRestoredHook([source=hooks.publishedSource,
                              invalidate=hooks.invalidateDerived](Simulation& state){
        if(invalidate)invalidate();
        const auto current=source();
        state.world().eachExisting<CaveRouteFollower>(
            [&](Entity,CaveRouteFollower& route){
                if(!current||route.sourceRevision!=current->revision())
                    route.needsRepath=true;
            });
    });
}
} // namespace aiverse::nav
