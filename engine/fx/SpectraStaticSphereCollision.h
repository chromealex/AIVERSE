// Static analytic sphere boundary, shared CPU/GPU collision contract.
// No world pointers. Centre and obstacle radius are in particle simulation metres.
#pragma once
#include "engine/render/Math3D.h"
#include <cmath>
namespace aiverse::fx {
inline bool resolveStaticSphere(render::float3 from,render::float3& to,
        render::float3& velocity,render::float3 center,float radius,
        float bounce,float dampen) {
    if(radius<=0)return false;
    const auto delta=to-from,m=from-center;
    const float len=render::length(delta),dist=render::length(m);
    render::float3 normal;
    if(dist<radius) {
        normal=dist>1e-6f?m*(1.f/dist):len>1e-6f?delta*(-1.f/len):render::float3{0,1,0};
    } else {
        if(len<=1e-6f)return false;
        const auto d=delta*(1.f/len);
        const float b=render::dot(m,d);
        if(b>=0)return false;
        const float c=render::dot(m,m)-radius*radius;
        const float disc=b*b-c;
        if(disc<0)return false;
        // Stable near-root form; avoids subtracting two large equal numbers.
        const float travel=c/(-b+std::sqrt(disc));
        if(travel<0||travel>len)return false;
        const auto at=m+d*travel;
        const float n=render::length(at);
        if(n<=1e-6f)return false;
        normal=at*(1.f/n);
    }
    to=center+normal*(radius+0.001f);
    const float vn=render::dot(velocity,normal);
    if(vn<0)velocity=(velocity-normal*vn)*(1-dampen)-normal*(vn*bounce);
    return true;
}
}
