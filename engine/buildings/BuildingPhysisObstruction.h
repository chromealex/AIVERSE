#pragma once
// Optional PHYSIS Box3D broad-phase guard for moving building leaves.
// Only include/link this file when the project already uses Box3D.
#include "engine/buildings/BuildingEcsObstruction.h"
#include "engine/physics/Scene.h"
#include <cstdint>

namespace aiverse::building {

inline bool buildingPhysisSweptOccupied(
    const physics::Scene& physicsScene,const world::Bounds& first,
    const world::Bounds& second,
    const physics::QueryFilter& filter={}){
    const auto occupied=[&](const world::Bounds& b){
        const Vec3 mid=(b.min+b.max)/2_fx;
        const Vec3 half=(b.max-b.min)/2_fx;
        return !physicsScene.overlapBox(
            {float(mid.x.toDouble()),float(mid.y.toDouble()),
             float(mid.z.toDouble())},
            {float(half.x.toDouble()),float(half.y.toDouble()),
             float(half.z.toDouble())},
            b3Quat_identity,filter).empty();
    };
    return occupied(first)||occupied(second);
}
inline auto buildingCommonPhysisSweptProbe(
    const World& ecs,const physics::Scene& physicsScene,
    physics::QueryFilter filter={}){
    return [&ecs,&physicsScene,filter](
        const world::Bounds& first,const world::Bounds& second,
        uint32_t firstLeafId,uint32_t secondLeafId){
        // firstLeafId / secondLeafId are SpatialWorld IDs, NOT
        // Box3D BodyHandles. The PHYSIS owner must omit moving
        // leaf bodies from its own query category/mask, or use the
        // shared SpatialWorld owner exclusively for those leaves.
        (void)firstLeafId;(void)secondLeafId;
        return buildingEcsSweptOccupied(ecs,first,second)||
               buildingPhysisSweptOccupied(
                   physicsScene,first,second,filter);
    };
}
} // namespace aiverse::building
