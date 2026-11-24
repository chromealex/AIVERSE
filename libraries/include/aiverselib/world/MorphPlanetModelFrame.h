// Strict native model-frame binding for planetary MORPH. No render or
// physics dependency; reuse the existing persistent GravityBinding units.
#pragma once
#include "MorphPhysisGravity.h"
#include <string_view>

namespace aiverse::morph {
struct PlanetModelFrame {
    Entity owner=kNullEntity;
    lib::worldprocess::SurfaceFrame frame{};
    int32_t unitsPerMetre=0;
};
inline std::optional<PlanetModelFrame> resolvePlanetModelFrame(
        const World& world,const game::Surface& surface,Entity owner,
        std::string_view body,int32_t unitsPerMetre){
    if(!world.alive(owner)||body.empty()||unitsPerMetre<1||unitsPerMetre>1000000)return {};
    const auto* wt=world.tryGetConst<WorldTransform>(owner);
    if(!wt)return {}; // legacy chart axes may be skewed; not a rigid model frame
    const auto frame=gravity_detail::ownerFrame(world,&surface,owner);
    if(!frame||surface.bodies[size_t(frame->body)].id!=body)return {};
    const auto one=Fixed::fromInt(1),eps=Fixed::fromRatio(1,100);
    for(auto v:{wt->scale.x,wt->scale.y,wt->scale.z})if(fxAbs(v-one)>eps)return {};
    // Full affine is the canonical render transform. Reject scaled/sheared
    // hierarchy and inconsistent translation, not merely the TRS summary.
    const Vec3 axes[]={frame->east,frame->up,frame->south};
    for(int col=0;col<3;++col){
        const auto& axis=axes[col];
        const Fixed values[]={axis.x,axis.y,axis.z};
        for(int row=0;row<3;++row)
            if(fxAbs(wt->affine.at(row,col)-values[row])>eps)return {};
    }
    const Fixed pos[]={frame->origin.x,frame->origin.y,frame->origin.z};
    for(int row=0;row<3;++row)
        if(fxAbs(wt->affine.at(row,3)-pos[row])>eps)return {};
    return PlanetModelFrame{owner,*frame,unitsPerMetre};
}
inline bool planetModelNodeCurrent(const World& world,const PlanetModelFrame& frame,
                                   Entity node,uint32_t completedTick){
    if(completedTick==UINT32_MAX||!world.alive(node))return false;
    const auto* n=world.tryGetConst<Node>(node);
    const auto* binding=world.tryGetConst<GravityBinding>(node);
    return n&&n->enabled&&valid(*n)&&n->sampledTick==completedTick&&
        binding&&binding->enabled&&valid(*binding)&&binding->frameOwner==frame.owner&&
        binding->unitsPerMetre==frame.unitsPerMetre;
}
} // namespace aiverse::morph
