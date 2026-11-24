// Example forwarding adapter; source collision publisher is shared.
#pragma once
#include "aiverselib/world/PlanetSourcePublisher.h"
#include "engine/render/Renderer3D.h"
namespace aiverse::examples::world_processes_native {
using lib::worldprocess::PlanetSourceCollider;
using lib::worldprocess::PlanetSourcePublisher;
inline std::optional<render::mat4> consequenceModelMatrix(
        const lib::worldprocess::ConsequenceMotion& motion,Vec3 anchor,
        const render::mat4& modelFromBody,render::float3 translation={}){
    const auto basis=fxQuaternionBasis(motion.rotation);
    if(!basis)return {};
    render::mat4 pose;
    const Vec3 axes[]={basis->right,basis->up,basis->forward,motion.position-anchor};
    for(int col=0;col<4;++col){
        const auto& v=axes[col];
        pose.at(0,col)=float(v.x.toDouble());
        pose.at(1,col)=float(v.y.toDouble());
        pose.at(2,col)=float(v.z.toDouble());
    }
    pose.at(0,3)+=translation.x;pose.at(1,3)+=translation.y;pose.at(2,3)+=translation.z;
    return pose*modelFromBody;
}
} // namespace aiverse::examples::world_processes_native
