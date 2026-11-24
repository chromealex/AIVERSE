// Optional Box3D adapter; include only in a host linked to AIVERSE::physics.
#pragma once
#include "engine/fx/SpectraSceneCollision.h"
#include "engine/physics/Scene.h"
namespace aiverse::fx {
// Host owns a stable read phase and Scene lifetime through each FX update.
// This adapter is a point ray only. Nonzero particle radii need a real
// shape-sweep provider; do not silently approximate them with point rays.
inline SceneCollisionRegistry::Sweep makeBox3DPointSweep(
        const physics::Scene& scene,physics::QueryFilter filter={}) {
    return [&scene,filter](const SceneCollisionQuery& q,SceneCollisionHit& h) {
        if(q.radius!=0)return false;
        const auto delta=q.to-q.from;
        if(render::length(delta)<=1e-8f)return false;
        const auto hit=scene.raycast({q.from.x,q.from.y,q.from.z},
                                     {delta.x,delta.y,delta.z},filter);
        if(hit.body==physics::kNoBody)return false;
        h.fraction=hit.fraction;h.normal={hit.normal.x,hit.normal.y,hit.normal.z};
        return true;
    };
}
}
