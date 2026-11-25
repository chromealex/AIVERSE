// SpectraFX native socket pose adapter. Consume existing View/MotionWeave
// model-space node matrices; never evaluate an animation graph or retain a rig.
#pragma once
#include "engine/view/ModelComposition.h"
#include "engine/fx/SpectraGraphRuntime.h"
#include <cmath>
namespace aiverse::spectra {
struct SocketEmissionPose {
    fx::float3 position{},direction{0,1,0};
};
// Host holds the current model read lease and prepared pose during this call.
// Missing evaluated pose fails closed unless static bind pose is opted in.
inline bool resolveSocketEmissionPose(const render::Model& rig,uint64_t publication,
        const view::CompiledModelSocket& binding,const render::mat4& parentWorld,
        std::span<const render::mat4> nodeWorld,const render::mat4& local,
        fx::float3 localDirection,SocketEmissionPose& out,bool allowBindPose=false) {
    if(!binding.matches(rig,publication,binding.socketName,binding.authoredSocket) ||
       (binding.resolvedSocket>=0 && nodeWorld.size()!=rig.nodes.size() &&
        (!allowBindPose || !nodeWorld.empty())))return false;
    render::ModelDraw parent{},child{},composed{};
    parent.world=parentWorld;
    child.model=&rig;
    child.world=render::mat4::identity();
    if(!view::composeAttachment(parent,rig,nodeWorld,child,
                                binding.resolvedSocket,local,composed))return false;
    for(float v:composed.world.m)if(!std::isfinite(v))return false;
    if(std::abs(composed.world.at(3,0))>1e-5f ||
       std::abs(composed.world.at(3,1))>1e-5f ||
       std::abs(composed.world.at(3,2))>1e-5f ||
       std::abs(composed.world.at(3,3)-1)>1e-5f)return false;
    const auto p=composed.world.transformPoint({0,0,0});
    // Linear 3x3 transform: translation does not affect a direction.
    const auto& m=composed.world;
    const fx::float3 d{
        m.at(0,0)*localDirection.x+m.at(0,1)*localDirection.y+m.at(0,2)*localDirection.z,
        m.at(1,0)*localDirection.x+m.at(1,1)*localDirection.y+m.at(1,2)*localDirection.z,
        m.at(2,0)*localDirection.x+m.at(2,1)*localDirection.y+m.at(2,2)*localDirection.z};
    const double length=std::sqrt(double(d.x)*d.x+double(d.y)*d.y+double(d.z)*d.z);
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
       !std::isfinite(length)||length<=1e-8)return false;
    out={p,{float(d.x/length),float(d.y/length),float(d.z/length)}};
    return true;
}
inline bool burstAtSocket(GraphRuntime& runtime,const SocketEmissionPose& pose,
                          fx::float4 tint={1,1,1,1}) {
    return runtime.burstAt(pose.position,pose.direction,tint);
}
}
