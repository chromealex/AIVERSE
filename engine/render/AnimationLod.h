#pragma once
#include "Math3D.h"
#include <cmath>
namespace aiverse::render {
struct AnimationLod { bool visible; float interval; };
// Shared projected-size policy; unlike distance, this also works with orthographic cameras.
inline AnimationLod projectedAnimationLod(const mat4& viewProj,float3 center,float3 top){
    const float4 c=viewProj*float4{center.x,center.y,center.z,1};
    const float4 t=viewProj*float4{top.x,top.y,top.z,1};
    const bool visible=c.w>0&&std::fabs(c.x)<c.w*1.2f&&std::fabs(c.y)<c.w*1.2f;
    const float extent=c.w>0&&t.w>0?std::fabs(t.y/t.w-c.y/c.w):0;
    return {visible,!visible?1e9f:extent>0.012f?0.0f:extent>0.004f?1.0f/30.0f:0.1f};
}
}
