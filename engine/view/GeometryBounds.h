// Generic presentation-space bounds transformation.
// Inputs and output are renderer float math; no gameplay or physics authority.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>

namespace aiverse::view {
struct WorldBounds {
    render::float3 min{},max{};
    bool valid=false;
};

// Transform all eight corners, allowing arbitrary rotation, reflection and
// non-uniform scale. Invalid AABBs are rejected rather than entering culling
// or presentation-only contact queries.
inline WorldBounds transformAabb(render::float3 localMin,
                                 render::float3 localMax,
                                 const render::mat4& world) {
    auto finite=[](render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    };
    if(!finite(localMin)||!finite(localMax)||
       localMin.x>localMax.x||localMin.y>localMax.y||localMin.z>localMax.z)
        return {};
    WorldBounds result;
    for(float x:{localMin.x,localMax.x})
        for(float y:{localMin.y,localMax.y})
            for(float z:{localMin.z,localMax.z}) {
                const auto point=world.transformPoint({x,y,z});
                if(!finite(point))return {};
                if(!result.valid) {
                    result.min=result.max=point;
                    result.valid=true;
                } else {
                    result.min={std::min(result.min.x,point.x),
                                std::min(result.min.y,point.y),
                                std::min(result.min.z,point.z)};
                    result.max={std::max(result.max.x,point.x),
                                std::max(result.max.y,point.y),
                                std::max(result.max.z,point.z)};
                }
            }
    return result;
}
} // namespace aiverse::view
