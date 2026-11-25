// Read-only PHYSIS support policy for any surface locomotion consumer.
// Does not integrate movement, provide adhesion or resolve collisions.
#pragma once
#include "WorldProcessSurfaceFrame.h"
#include "engine/physis/PhysisGravity.h"
#include <optional>

namespace aiverse::lib::worldprocess {
enum class GravitySupportStatus { Supported, Airborne, WeakGravity, Outward, TooSteep };
struct GravitySupportSettings {
    Fixed groundTolerance=Fixed::fromRatio(1,8); // metres
    Fixed minimumGravity=Fixed::fromRatio(1,20); // m/s^2
    Fixed minimumInwardCosine=Fixed::fromRatio(1,2); // <=60 degrees to inward terrain normal
};
inline bool valid(const GravitySupportSettings& settings){
    return settings.groundTolerance>=Fixed{}&&settings.groundTolerance<=Fixed::fromInt(2)&&
        settings.minimumGravity>Fixed{}&&settings.minimumGravity<=Fixed::fromInt(200)&&
        settings.minimumInwardCosine>Fixed{}&&settings.minimumInwardCosine<=Fixed::fromInt(1);
}
struct GravitySupportSample {
    int body=-1,cell=-1;
    Vec3 acceleration{},normal{},tangentAcceleration{};
    Fixed signedClearance{},inwardAcceleration{};
    GravitySupportStatus status=GravitySupportStatus::Airborne;
    bool supported()const{return status==GravitySupportStatus::Supported;}
};
// Read canonical terrain slope, NOT radial up alone. The optional normal
// for a planar host must come from its existing ground/collision owner.
// No implicit PHYSIS field creation or movement-layer switch.
inline std::optional<GravitySupportSample> classifyGravitySupport(
        Vec3 acceleration,Vec3 outwardNormal,Fixed signedClearance,
        GravitySupportSettings settings={}){
    if(!valid(settings))return {};
    constexpr auto cap=Fixed::fromInt(200);
    for(auto v:{acceleration.x,acceleration.y,acceleration.z})
        if(v< -cap||v>cap)return {};
    for(auto v:{outwardNormal.x,outwardNormal.y,outwardNormal.z})
        if(v< -Fixed::fromInt(2)||v>Fixed::fromInt(2))return {};
    if(outwardNormal.lengthSq()<Fixed::fromRatio(1,4))return {};
    GravitySupportSample sample;
    sample.acceleration=acceleration;sample.normal=fxNormalize(outwardNormal);
    sample.signedClearance=signedClearance;
    sample.inwardAcceleration=-acceleration.dot(sample.normal);
    sample.tangentAcceleration=acceleration+sample.normal*sample.inwardAcceleration;
    const auto magnitude=acceleration.length();
    if(signedClearance>settings.groundTolerance||signedClearance< -settings.groundTolerance)
        sample.status=GravitySupportStatus::Airborne;
    else if(magnitude<settings.minimumGravity)
        sample.status=GravitySupportStatus::WeakGravity;
    else if(sample.inwardAcceleration<=Fixed{})
        sample.status=GravitySupportStatus::Outward;
    else if(sample.inwardAcceleration<settings.minimumGravity||
            sample.inwardAcceleration<magnitude*settings.minimumInwardCosine)
        sample.status=GravitySupportStatus::TooSteep;
    else sample.status=GravitySupportStatus::Supported;
    return sample;
}
inline std::optional<GravitySupportSample> samplePlanetGravitySupport(
        const World& world,const game::Surface& surface,Vec3 position,
        GravitySupportSettings settings={}){
    const auto frame=surfaceFrame(&surface,position);
    if(!frame)return {};
    int face=0;Fixed a{},b{};
    surface.cubeOf(frame->body).faceFromDir(position-game::Surface::anchor(frame->body),face,a,b);
    const auto normal=surface.faceNormal(frame->body,face,a,b);
    auto sample=classifyGravitySupport(physis::gravityAt(world,position),normal,
        surface.altitude(position)-surface.heightAt(position),settings);
    if(sample){sample->body=frame->body;sample->cell=frame->cell;}
    return sample;
}
} // namespace aiverse::lib::worldprocess
