#pragma once
// Stage44: project-independent actor motor STEERING over cave route data.
// This does not mutate CharacterBody, Transform, or native physics. It only
// selects a normal actor::Input for the existing authoritative moveActor /
// moveActorModes motor. An explicit ActorHost opt-in calls this before motor.
#include "engine/nav/CaveSimulationNavigation.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"
#include "engine/actor/ActorComponents.h"
#include "engine/core/FixedMath.h"
#include <cstdint>
#include <memory>

namespace aiverse::nav {
struct CaveActorMotorSteering {
    uint32_t lookAheadCm=75;
    uint32_t waypointToleranceCm=40;
    uint32_t maxStepCm=100;
};
inline bool steerCaveActorMotor(
    World& ecs,Entity entity,actor::Input& input,
    const terraweave::caverns::CaveNavigationDataProvider& source,
    const world::SpatialWorld& collision,
    CaveActorMotorSteering settings={}) {
    using namespace terraweave::caverns;
    if(!ecs.alive(entity)||!ecs.has<CaveRouteFollower>(entity)||
       !ecs.has<actor::CharacterBody>(entity)||
       settings.waypointToleranceCm>1000||
       !settings.lookAheadCm||settings.lookAheadCm>10000||
       !settings.maxStepCm||settings.maxStepCm>10000)
        return false;
    auto& route=ecs.get<CaveRouteFollower>(entity);
    const auto& body=ecs.get<actor::CharacterBody>(entity);
    if(route.needsRepath||route.arrived||route.waypoints.empty())
        return false;
    if(route.sourceRevision!=source.revision()||
       route.environmentRevision!=source.environmentRevision()){
        route.needsRepath=true;return false;
    }
    const auto centimeters=[](Fixed f)->int64_t {
        return f.raw*100/Fixed::kOne;
    };
    const PointCm here{centimeters(body.pos.x),
                       centimeters(body.pos.y),
                       centimeters(body.pos.z)};
    auto close=[&](PointCm b)->bool {
        const int64_t dx=here.x-b.x,dy=here.y-b.y,dz=here.z-b.z;
        return dx*dx+dy*dy+dz*dz<=
            int64_t(settings.waypointToleranceCm)*
            settings.waypointToleranceCm;
    };
    while(route.current<route.waypoints.size()&&
          close(route.waypoints[route.current]))++route.current;
    if(route.current>=route.waypoints.size()){
        route.arrived=true;input.forward=input.right=0;return true;
    }
    const auto target=route.waypoints[route.current];
    if(!source.canTraverse(here,target,route.agent)){
        route.needsRepath=true;input.forward=input.right=0;return false;
    }
    // The existing motor owns all actual movement, collision, gravity,
    // crouch, stairs and body state. Steering only changes requested yaw
    // and forward axis, preserving firing/actions/buttons from the caller.
    const Fixed dx=Fixed::fromRatio(target.x-here.x,100);
    const Fixed dz=Fixed::fromRatio(target.z-here.z,100);
    if(dx.raw==0&&dz.raw==0){
        // Do not fabricate vertical flight with a grounded actor motor.
        // Vertical routes require the owner's actual flight/swim/climb mode.
        input.forward=input.right=0;
        return false;
    }
    const Fixed yaw=fxAtan2Turns(dx,dz);
    const Fixed heading=yaw<Fixed{}?yaw+Fixed::fromInt(1):yaw;
    const Vec3 start=body.pos+Vec3{{},Fixed::fromRatio(route.agent.heightCm,200),{}};
    const Vec3 delta{dx,{},dz};
    const Fixed radius=Fixed::fromRatio(route.agent.radiusCm,100);
    if(collision.cast(start,delta,{radius,
        Fixed::fromRatio(route.agent.heightCm,200),radius}).hit){
        route.needsRepath=true;input.forward=input.right=0;
        return false;
    }
    input.yaw=heading;
    input.forward=1000;
    input.right=0;
    return true;
}

// Repath happens IN the existing ActorHost's authoritative actor tick,
// BEFORE the native character motor. It reads CharacterBody (not Transform)
// and cannot create a second movement owner.
inline CaveRepathTickStats repathCaveActorRoutes(
    Simulation& sim,
    const terraweave::caverns::CaveNavigationDataProvider& provider,
    uint32_t budget=8) {
    using namespace terraweave::caverns;
    CaveRepathTickStats out;
    sim.world().each<CaveRouteIntent,CaveRouteFollower,actor::CharacterBody>(
        [&](Entity entity,CaveRouteIntent& intent,CaveRouteFollower& route,
            actor::CharacterBody& body) {
        (void)entity;
        if(!intent.active||out.attempted>=budget||body.vehicle)return;
        if(route.sourceRevision!=provider.revision()||
           route.environmentRevision!=provider.environmentRevision()){
            if(!route.needsRepath)intent.repathNotBeforeTick=sim.tick();
            route.needsRepath=true;
        }
        if(!route.needsRepath||sim.tick()<intent.repathNotBeforeTick)return;
        CavePathBackend backend=CavePathBackend::AStar;
        if(intent.algorithm=="hpa")backend=CavePathBackend::HPAStar;
        else if(intent.algorithm=="flow")backend=CavePathBackend::FlowField;
        else if(intent.algorithm=="hybrid")backend=CavePathBackend::Hybrid;
        else if(intent.algorithm!="astar"){
            ++out.unavailable;return; // custom engines supply own actor hook
        }
        ++out.attempted;
        const auto cm=[](Fixed f)->int64_t {
            return f.raw*100/Fixed::kOne;
        };
        CavePathQuery request;
        request.start={cm(body.pos.x),cm(body.pos.y),cm(body.pos.z)};
        request.goal=intent.goal;
        request.agent=route.agent;
        request.sourceRevision=provider.revision();
        request.maxVisits=intent.maxVisits;
        auto path=solveCaveGraph(request,
            std::shared_ptr<const CaveNavigationDataProvider>(
                &provider,[](const CaveNavigationDataProvider*){}),
            backend);
        if(!path.reached || path.waypoints.empty()){
            ++out.failed;
            if(intent.attempts<UINT32_MAX)++intent.attempts;
            const auto delay=std::max(1u,intent.retryTicks);
            intent.repathNotBeforeTick=sim.tick()>UINT32_MAX-delay
                ?UINT32_MAX:sim.tick()+delay;
            return;
        }
        route.waypoints=std::move(path.waypoints);
        route.sourceRevision=provider.revision();
        route.environmentRevision=provider.environmentRevision();
        route.current=0;
        route.arrived=false;route.needsRepath=false;
        intent.attempts=0;intent.repathNotBeforeTick=sim.tick();
        ++out.assigned;
    });
    return out;
}
} // namespace aiverse::nav
