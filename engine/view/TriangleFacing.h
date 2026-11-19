// Generic triangle-facing and normal computation for any View mesh.
// Road/terrain/planet hosts supply vertices and the outward-facing hint.
// Never reads game, physics, collider, entity or rendering backend data.
#pragma once
#include "engine/render/Math3D.h"
#include <cmath>

namespace aiverse::view {
struct TriangleFacing {
    render::float3 normal{0,1,0};
    bool valid=false,flip=false;
};
inline TriangleFacing orientTriangleTo(
        render::float3 a,render::float3 b,render::float3 c,
        render::float3 outwardHint) {
    const auto normal=render::cross(b-a,c-a);
    const float areaSq=render::dot(normal,normal);
    if(!(areaSq>=1.e-14f) || !std::isfinite(areaSq))
        return {};
    // A non-finite outward hint must not silently select an arbitrary winding.
    // Reject before the dot product; a NaN comparison would otherwise be false.
    if(!std::isfinite(outwardHint.x) || !std::isfinite(outwardHint.y) ||
       !std::isfinite(outwardHint.z))
        return {};
    const bool flip=render::dot(normal,outwardHint)<0.f;
    return {render::normalize(flip?-normal:normal),true,flip};
}
} // namespace aiverse::view
