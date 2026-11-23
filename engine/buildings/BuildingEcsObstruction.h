#pragma once
// Universal ECS/PHYSIS swept occupancy adapter. No FPS/RTS actor types.
// The authoritative simulation supplies one shared ECS World and a PHYSIS
// Scene if it uses Box3D. The caller owns both and serializes this query
// with the authoritative physics step.
#include "engine/sim/CommonComponents.h"
#include "engine/world/SpatialWorld.h"
#include "engine/physics/Scene.h"
#include <functional>
#include <cstdint>

namespace aiverse::building {

// Body bounds are gameplay-defined; Transform alone does NOT establish a
// physical volume. One component per collidable ECS entity with positive
// local half extents. Other systems may use their own adapters as long as
// the physical swept callback uses the same canonical ECS/PHYSIS world.
struct BuildingPhysicalExtent {
    Vec3 halfExtents{};
    bool blocksBuildingMotion=true;
};

inline bool buildingEcsSweptOccupied(
    const World& ecs,const world::Bounds& first,
    const world::Bounds& second){
    bool occupied=false;
    ecs.eachExistingConst<BuildingPhysicalExtent>(
        [&](Entity entity,const BuildingPhysicalExtent& volume){
            if(occupied||!volume.blocksBuildingMotion||!ecs.alive(entity))
                return;
            const auto* local=ecs.tryGetConst<Transform>(entity);
            if(!local||volume.halfExtents.x<=Fixed{}||
               volume.halfExtents.y<=Fixed{}||
               volume.halfExtents.z<=Fixed{})return;
            const Vec3 centre=local->pos;
            const world::Bounds box{
                centre-volume.halfExtents,centre+volume.halfExtents};
            if(world::overlaps(first,box)||world::overlaps(second,box))
                occupied=true;
        });
    return occupied;
}

// Optional Box3D support queries the EXISTING physics::Scene. It is
// read-only and uses the physics engine's real rotated collision shapes;
// unlike an AABB-only fallback it can see kinematic rigid bodies too.
// The caller must exclude its own door bodies from these queries, or use
// dedicated SpatialWorld leaves not registered as Box3D bodies.
inline bool buildingPhysisSweptOccupied(
    const physics::Scene& physis,const world::Bounds& a,
    const world::Bounds& b,
    const physics::QueryFilter& filter={}){
    const auto overlap=[&](const world::Bounds& bounds){
        const Vec3 center=(bounds.min+bounds.max)/2;
        const Vec3 half=(bounds.max-bounds.min)/2;
        return !physis.overlapBox(
            {float(center.x.toDouble()),float(center.y.toDouble()),
             float(center.z.toDouble())},
            {float(half.x.toDouble()),float(half.y.toDouble()),
             float(half.z.toDouble())},b3Quat_identity,filter).empty();
    };
    return overlap(a)||overlap(b);
}

inline auto buildingCommonSweptProbe(
    const World& ecs,const physics::Scene* physis=nullptr,
    physics::QueryFilter filter={}){
    return [&ecs,physis,filter](
        const world::Bounds& first,const world::Bounds& second,
        uint32_t ownFirst,uint32_t ownSecond){
        // The two SpatialWorld IDs are not Box3D handles. PHYSIS owner
        // must not publish those same leaves as queryable Box3D bodies.
        (void)ownFirst;(void)ownSecond;
        return buildingEcsSweptOccupied(ecs,first,second)||
               (physis&&buildingPhysisSweptOccupied(
                   *physis,first,second,filter));
    };
}

} // namespace aiverse::building
