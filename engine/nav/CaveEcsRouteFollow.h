#pragma once
// Shared engine Stage42: ECS route follower with integer-centimetre paths.
// No genre, no game-specific actor vectors; owning Simulation explicitly
// registers this system on its deterministic fixed tick. Physics collision
// is a MANDATORY host callback (Box3D/SpatialWorld/actor controller).
// Never move a unit based on client render-only cave visuals.
#include "engine/core/ECS.h"
#include "engine/sim/CommonComponents.h"
#include "engine/world/TerraWeaveCavernsNavigationData.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::nav {
struct CaveRouteFollower {
    std::vector<terraweave::caverns::PointCm> waypoints;
    terraweave::caverns::CaveAgentProfile agent{};
    uint64_t sourceRevision=0;
    uint64_t environmentRevision=0;
    uint32_t current=0;
    uint32_t speedCmPerTick=25;
    bool arrived=false;
    bool needsRepath=false;
};
struct CaveRouteTickStats {
    uint32_t moved=0,arrived=0,blocked=0,stale=0;
};
using CaveMovementCollision=std::function<bool(
    Entity,terraweave::caverns::PointCm,
    terraweave::caverns::PointCm,
    const terraweave::caverns::CaveAgentProfile&)>;
inline terraweave::caverns::PointCm caveTransformCm(
    const Transform& transform)noexcept {
    const auto convert=[](Fixed f)->int64_t {
        // Divide first so even an out-of-domain Fixed value cannot overflow.
        return (f.raw/Fixed::kOne)*100+(f.raw%Fixed::kOne)*100/Fixed::kOne;
    };
    return {convert(transform.pos.x),convert(transform.pos.y),
            convert(transform.pos.z)};
}
inline void setCaveTransformCm(Transform& transform,
                               terraweave::caverns::PointCm p){
    transform.prevPos=transform.pos;
    transform.pos={
        Fixed::fromRatio(p.x,100),
        Fixed::fromRatio(p.y,100),
        Fixed::fromRatio(p.z,100)};
}
inline bool validCaveRoutePoint(terraweave::caverns::PointCm p) noexcept {
    constexpr auto limit=terraweave::caverns::kLimitCm;
    return p.x>=-limit&&p.x<=limit&&p.y>=-limit&&p.y<=limit&&
           p.z>=-limit&&p.z<=limit;
}
// Routes enter the canonical ECS component on the owning game tick, not
// into a per-genre actor list. The component must be serialized/hashed by
// the host's deterministic state schema for rollback/save games.
inline bool assignCaveRoute(World& world,Entity entity,
    const terraweave::caverns::CavePathResult& path,
    terraweave::caverns::CaveAgentProfile profile,
    uint32_t speedCmPerTick) {
    if(!world.alive(entity)||!path.reached||!path.sourceRevision||
       path.waypoints.empty()||path.waypoints.size()>65536||
       !std::all_of(path.waypoints.begin(),path.waypoints.end(),validCaveRoutePoint)||
       !speedCmPerTick||speedCmPerTick>10000)
        return false;
    terraweave::caverns::CaveNavigationDataProvider::validateProfile(profile);
    CaveRouteFollower next;
    next.waypoints=path.waypoints;
    next.agent=profile;
    next.sourceRevision=path.sourceRevision;
    next.environmentRevision=path.environmentRevision;
    next.speedCmPerTick=speedCmPerTick;
    if(world.has<CaveRouteFollower>(entity))
        world.get<CaveRouteFollower>(entity)=std::move(next);
    else world.add<CaveRouteFollower>(entity,std::move(next));
    return true;
}
// Call from the ACTUAL authoritative ECS game tick with a published SDF
// provider and a collision callback from the same committed physics epoch.
// No callback means fail closed: moving actors without Box3D/SpatialWorld
// verification would allow crossing dynamic bodies and stale cave walls.
inline CaveRouteTickStats updateCaveRouteFollowers(
    World& world,
    const terraweave::caverns::CaveNavigationDataProvider& source,
    const CaveMovementCollision& collision,
    uint32_t maxActors=65536) {
    using namespace terraweave::caverns;
    CaveRouteTickStats stats;
    uint32_t visited=0;
    world.each<CaveRouteFollower,Transform>(
        [&](Entity entity,CaveRouteFollower& route,Transform& transform){
        if(visited++>=maxActors)return;
        if(route.arrived||route.needsRepath)return;
        if(route.sourceRevision!=source.revision() ||
           route.environmentRevision!=source.environmentRevision()){
            route.needsRepath=true;++stats.stale;return;
        }
        if(route.current>=route.waypoints.size()){
            route.arrived=true;++stats.arrived;return;
        }
        if(!collision || !route.speedCmPerTick){
            ++stats.blocked;return;
        }
        PointCm now=caveTransformCm(transform);
        auto goal=route.waypoints[route.current];
        if(!validCaveRoutePoint(now)||!validCaveRoutePoint(goal)){
            route.needsRepath=true;++stats.blocked;return;
        }
        if(now.x==goal.x&&now.y==goal.y&&now.z==goal.z){
            if(++route.current>=route.waypoints.size()){
                route.arrived=true;++stats.arrived;
            }
            return;
        }
        const auto distance=[](int64_t a,int64_t b)->uint64_t{
            return uint64_t(a>b?a-b:b-a);
        };
        const uint64_t delta=std::max({
            distance(now.x,goal.x),
            distance(now.y,goal.y),
            distance(now.z,goal.z)});
        if(!delta){++route.current;return;}
        const uint64_t step=std::min<uint64_t>(delta,route.speedCmPerTick);
        const auto advance=[step,delta](int64_t a,int64_t b){
            const int64_t signedDelta=b-a;
            // All cave world coordinates are bounded to +/-1e9 cm, so the
            // 2e9*speedCmPerTick product must be capped by caller settings.
            const int64_t move=(signedDelta*int64_t(step))/int64_t(delta);
            return a+move;
        };
        if(step>10000) {
            route.needsRepath=true;++stats.blocked;return;
        }
        PointCm next{advance(now.x,goal.x),
                     advance(now.y,goal.y),
                     advance(now.z,goal.z)};
        if(next.x==now.x&&next.y==now.y&&next.z==now.z) {
            // Deterministically advance along the dominant axis rather
            // than stalling forever when fixed integer interpolation rounds.
            if(distance(now.x,goal.x)==delta)
                next.x+=goal.x>now.x?1:-1;
            else if(distance(now.y,goal.y)==delta)
                next.y+=goal.y>now.y?1:-1;
            else next.z+=goal.z>now.z?1:-1;
        }
        if(!source.canTraverse(now,next,route.agent) ||
           !collision(entity,now,next,route.agent)){
            route.needsRepath=true;++stats.blocked;return;
        }
        setCaveTransformCm(transform,next);
        ++stats.moved;
        if(next.x==goal.x&&next.y==goal.y&&next.z==goal.z) {
            if(++route.current>=route.waypoints.size()){
                route.arrived=true;++stats.arrived;
            }
        }
    });
    return stats;
}
} // namespace aiverse::nav
