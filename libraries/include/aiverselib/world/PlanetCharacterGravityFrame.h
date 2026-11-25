// Canonical orientation for existing character/collision owners. No movement
// integration or additional gravity field; the caller owns heading continuity.
#pragma once
#include "SurfaceGravitySupport.h"
#include "engine/core/FixedQuaternion.h"
#include <string_view>

namespace aiverse::lib::worldprocess {
struct PlanetCharacterGravityFrame {
    int body=-1;
    Vec3 acceleration{},up{};
    Fixed magnitude{};
    FixedQuaternion rotation{};
    bool allowGround=false;
};
inline std::optional<PlanetCharacterGravityFrame> planetCharacterGravityFrame(
        const World& world,const game::Surface& surface,Vec3 position,
        std::string_view body,Vec3 forward,Fixed minimumGravity=Fixed::fromRatio(1,20)){
    if(minimumGravity<=Fixed{}||minimumGravity>Fixed::fromInt(200))return {};
    const auto frame=surfaceFrame(&surface,position);
    if(!frame||surface.bodies[size_t(frame->body)].id!=body)return {};
    for(auto v:{forward.x,forward.y,forward.z})if(v< -2_fx||v>2_fx)return {};
    PlanetCharacterGravityFrame out;
    out.body=frame->body;out.acceleration=physis::gravityAt(world,position);
    for(auto v:{out.acceleration.x,out.acceleration.y,out.acceleration.z})
        if(v< -200_fx||v>200_fx)return {};
    out.magnitude=out.acceleration.length();
    out.allowGround=out.magnitude>=minimumGravity;
    // Even weak nonzero gravity uses its actual direction whenever Fixed
    // normalization is reliable. Near-zero uses radial orientation only.
    out.up=out.magnitude>Fixed::fromRatio(1,100)?fxNormalize(out.acceleration*Fixed::fromInt(-1)):frame->up;
    if(out.magnitude<=Fixed::fromRatio(1,100))out.magnitude={};
    auto tangent=[&](Vec3 v){return v-out.up*v.dot(out.up);};
    auto z=tangent(forward);
    if(z.lengthSq()<Fixed::fromRatio(1,100))z=tangent(frame->south);
    if(z.lengthSq()<Fixed::fromRatio(1,100))z=tangent(frame->east);
    const auto q=fxQuaternionLookRotation(z,out.up);
    if(!q)return {};
    out.rotation=*q;return out;
}
} // namespace aiverse::lib::worldprocess
