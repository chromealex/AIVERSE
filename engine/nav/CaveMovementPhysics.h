#pragma once
// Stage43: authoritative collision callback for shared ECS Cave routes.
// Uses the SAME native SpatialWorld and optional Box3D Scene as game actors.
// SpatialWorld uses deterministic Fixed; Box3D query is optional ONLY for
// authoritative server/host simulation, not cross-platform lockstep clients.
#include "engine/nav/CaveEcsRouteFollow.h"
#include "engine/world/SpatialWorld.h"
#include "engine/physics/Scene.h"
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace aiverse::nav {
using CaveSelfBody=std::function<physics::BodyHandle(Entity)>;
struct CaveMovementPhysicsSettings {
    bool checkBox3D=false;
    bool checkSpatialWorld=true;
    uint64_t box3dMask=~0ull;
};
inline CaveMovementCollision makeCaveMovementPhysics(
    const world::SpatialWorld& spatial,
    const physics::Scene* box3d=nullptr,
    CaveSelfBody selfBody={},
    CaveMovementPhysicsSettings config={}) {
    if(config.checkBox3D&&!box3d)
        throw std::invalid_argument("Caverns missing native Box3D authority");
    if(!config.checkSpatialWorld&&!config.checkBox3D)
        throw std::invalid_argument("Caverns movement must check native collision");
    return [&spatial,box3d,selfBody=std::move(selfBody),config](
        Entity entity,terraweave::caverns::PointCm from,
        terraweave::caverns::PointCm to,
        const terraweave::caverns::CaveAgentProfile& profile)->bool {
        const auto cm=[](int64_t v){return Fixed::fromRatio(v,100);};
        const Fixed halfHeight=Fixed::fromRatio(profile.heightCm,200);
        const Vec3 start{cm(from.x),cm(from.y)+halfHeight,cm(from.z)};
        const Vec3 destination{cm(to.x),cm(to.y)+halfHeight,cm(to.z)};
        const Vec3 delta=destination-start;
        const Vec3 half{cm(profile.radiusCm),halfHeight,
                        cm(profile.radiusCm)};
        if(config.checkSpatialWorld){
            if(spatial.cast(start,delta,half).hit)return false;
            const world::Bounds destinationBounds{destination-half,
                                                   destination+half};
            if(spatial.blocked(destinationBounds))return false;
        }
        if(config.checkBox3D){
            // A native Box3D Scene query is a game authority decision, never
            // a deterministic peer-to-peer source. Character handles are
            // ignored to avoid rejecting their own capsule.
            physics::QueryFilter filter;
            filter.mask=config.box3dMask;
            filter.ignore=selfBody?selfBody(entity):physics::kNoBody;
            const auto f=[](Fixed x){return float(x.toDouble());};
            const b3Vec3 a{f(start.x),f(start.y),f(start.z)};
            const b3Vec3 d{f(delta.x),f(delta.y),f(delta.z)};
            if(box3d->raycast(a,d,filter).body!=physics::kNoBody)
                return false;
            const b3Vec3 dest{f(destination.x),f(destination.y),
                              f(destination.z)};
            const b3Vec3 bottom{dest.x,dest.y-f(halfHeight),dest.z};
            const b3Vec3 top{dest.x,dest.y+f(halfHeight),dest.z};
            if(!box3d->overlapCapsule(bottom,top,
                        f(cm(profile.radiusCm)),filter).empty())
                return false;
        }
        return true;
    };
}
} // namespace aiverse::nav
