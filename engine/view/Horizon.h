// Reusable curved-surface horizon occlusion in View presentation.
// One immutable horizon plan per camera/body; many cheap chunk queries.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
namespace aiverse::view {
struct SphericalHorizon {
    render::float3 eyeDirection{};
    float occluderRadius=0.f, baseAngle=0.f;
    bool active=false;

    static SphericalHorizon fromLocalCamera(render::float3 cameraLocal,
                                            float radius) {
        SphericalHorizon result;
        const float eyeRadius=render::length(cameraLocal);
        if(!(eyeRadius>radius) || !(radius>0.f))
            return result; // camera inside the sphere: conservatively visible
        result.eyeDirection=cameraLocal*(1.f/eyeRadius);
        result.occluderRadius=radius;
        result.baseAngle=std::acos(radius/eyeRadius);
        result.active=true;
        return result;
    }
    bool hidden(render::float3 capDirection,float cosCapHalfAngle,
                float maxSurfaceRadius) const {
        if(!active)return false;
        const float eyeAngle=std::acos(std::clamp(
            render::dot(eyeDirection,capDirection),-1.f,1.f));
        const float capAngle=std::acos(
            std::clamp(cosCapHalfAngle,-1.f,1.f));
        const float horizonAngle=baseAngle+std::acos(
            std::clamp(occluderRadius/
                std::max(maxSurfaceRadius,occluderRadius),-1.f,1.f));
        return eyeAngle-capAngle>horizonAngle;
    }
};
// Convenience overload for one-off spherical caps.
inline bool sphericalCapHiddenByHorizon(
        render::float3 cameraLocal,float occluderRadius,
        render::float3 capDirection,float cosCapHalfAngle,
        float maxSurfaceRadius) {
    return SphericalHorizon::fromLocalCamera(cameraLocal,occluderRadius)
        .hidden(capDirection,cosCapHalfAngle,maxSurfaceRadius);
}
} // namespace aiverse::view
