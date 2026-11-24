// Read PHYSIS once per sample, integrate only through canonical MORPH.
#pragma once
#include "MorphGravityBinding.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/core/FixedQuaternion.h"
#include "engine/sim/CommonComponents.h"
#include "engine/physis/PhysisGravity.h"

namespace aiverse::morph {
namespace gravity_detail {
inline bool boundedPosition(Vec3 p){
    constexpr auto bound=Fixed::fromInt(1000000);
    for(auto v:{p.x,p.y,p.z})if(v< -bound||v>bound)return false;
    return true;
}
inline std::optional<lib::worldprocess::SurfaceFrame> ownerFrame(
        const World& read,const game::Surface* surface,Entity owner){
    const auto* tr=read.tryGetConst<Transform>(owner);
    const auto* wt=read.tryGetConst<WorldTransform>(owner);
    if(!tr&&!wt)return {};
    const Vec3 origin=wt?wt->position:tr->pos;
    if(!boundedPosition(origin))return {};
    auto frame=lib::worldprocess::surfaceFrame(surface,origin);
    if(!frame)return {};
    if(wt){
        // WorldTransform rotation already lives in simulation space. Do
        // not rotate it into the terrain basis a second time.
        const auto& q=wt->rotation;
        const auto basis=fxQuaternionBasis({q.x,q.y,q.z,q.w});
        if(!basis)return {};
        frame->east=basis->right;frame->up=basis->up;frame->south=basis->forward;
    }else{
        // Legacy Transform has yaw only, expressed about local surface up.
        const auto c=fxCosTurns(tr->yaw),s=fxSinTurns(tr->yaw);
        const auto east=frame->east,south=frame->south;
        frame->east=east*c-south*s;
        frame->south=east*s+south*c;
    }
    return frame;
}
} // namespace gravity_detail
inline std::optional<std::array<I,3>> physisNodeLoad(
        const World& read,const game::Surface* surface,Entity e,const Node& n){
    const auto* binding=read.tryGetConst<GravityBinding>(e);
    if(!binding)return std::array<I,3>{};
    if(!valid(*binding))return {};
    if(!binding->enabled||!binding->impulseMilli)return std::array<I,3>{};
    const auto frame=gravity_detail::ownerFrame(read,surface,binding->frameOwner);
    if(!frame)return {};
    const Vec3 local{Fixed::fromRatio(n.x,binding->unitsPerMetre),
        Fixed::fromRatio(n.y,binding->unitsPerMetre),Fixed::fromRatio(n.z,binding->unitsPerMetre)};
    const Vec3 point=frame->origin+frame->simulationVector(local);
    if(!gravity_detail::boundedPosition(point))return {};
    if(surface){
        const auto resolved=lib::worldprocess::surfaceFrame(surface,point);
        if(!resolved||resolved->body!=frame->body)return {};
    }
    auto g=physis::gravityAt(read,point);
    if(binding->relative)g=g-physis::gravityAt(read,frame->origin);
    const auto projected=frame->localVector(g);
    const auto scale=Fixed::fromRatio(binding->impulseMilli,1000);
    return std::array<I,3>{limit((projected.x*scale).toInt(),kForceLimit),
        limit((projected.y*scale).toInt(),kForceLimit),
        limit((projected.z*scale).toInt(),kForceLimit)};
}
inline bool advanceWithPhysisGravity(World& world,const game::Surface* surface,uint32_t tick){
    if(world.countExisting<GravityBinding>()>kMaxNodes)return false;
    return advanceWithLoad(world,tick,[&](const World& read,Entity e,const Node& n){
        return physisNodeLoad(read,surface,e,n);
    });
}
} // namespace aiverse::morph
