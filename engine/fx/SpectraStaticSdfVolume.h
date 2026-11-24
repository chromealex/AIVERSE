// Bounded immutable sampled signed-distance lattice, x-fastest, metres.
// No extrapolation. Segment sampling is approximate, not conservative CCD.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace aiverse::fx {
struct StaticSdfVolume {
    render::float3 origin{};
    float spacing=1;
    uint32_t dimension=0; // 0 = disabled; otherwise 3..16 inclusive
    std::vector<float> samples;
    bool valid()const {
        if(!dimension)return samples.empty();
        if(dimension<3||dimension>16||!std::isfinite(spacing)||spacing<0.01f||spacing>1000||
           samples.size()!=size_t(dimension)*dimension*dimension)return false;
        for(float v:{origin.x,origin.y,origin.z})
            if(!std::isfinite(v)||std::abs(v)>1e6f||std::abs(v+spacing*(dimension-1))>1e6f)return false;
        for(float v:samples)if(!std::isfinite(v)||std::abs(v)>100000.f)return false;
        return true;
    }
    bool sample(render::float3 p,float& distance,render::float3& gradient)const {
        if(dimension<3||dimension>16||spacing<=0||samples.size()!=size_t(dimension)*dimension*dimension)return false;
        const auto q=(p-origin)*(1.f/spacing);
        const float max=float(dimension-1);
        if(!(q.x>=0&&q.y>=0&&q.z>=0&&q.x<=max&&q.y<=max&&q.z<=max))return false;
        const uint32_t x=std::min(uint32_t(q.x),dimension-2),y=std::min(uint32_t(q.y),dimension-2),z=std::min(uint32_t(q.z),dimension-2);
        const float u=q.x-x,v=q.y-y,w=q.z-z;
        const auto at=[&](uint32_t i,uint32_t j,uint32_t k){return samples[(size_t(k)*dimension+j)*dimension+i];};
        const float a=at(x,y,z),b=at(x+1,y,z),c=at(x,y+1,z),d=at(x+1,y+1,z);
        const float e=at(x,y,z+1),f=at(x+1,y,z+1),g=at(x,y+1,z+1),h=at(x+1,y+1,z+1);
        const auto mix=[](float l,float r,float s){return l+(r-l)*s;};
        distance=mix(mix(mix(a,b,u),mix(c,d,u),v),mix(mix(e,f,u),mix(g,h,u),v),w);
        gradient={mix(mix(b-a,d-c,v),mix(f-e,h-g,v),w),
                  mix(mix(c-a,d-b,u),mix(g-e,h-f,u),w),
                  mix(mix(e-a,f-b,u),mix(g-c,h-d,u),v)};
        gradient=gradient*(1.f/spacing);
        return true;
    }
};
inline bool resolveStaticSdf(const StaticSdfVolume& sdf,render::float3 from,render::float3& to,
        render::float3& velocity,float radius,float bounce,float dampen) {
    if(!sdf.dimension)return false;
    const auto delta=to-from;
    const float len=render::length(delta);
    if(!std::isfinite(len))return false;
    const unsigned steps=unsigned(std::clamp(std::ceil(len/(sdf.spacing*0.5f)),1.f,64.f));
    float dist=0,previous=0;
    render::float3 normal{};
    if(!sdf.sample(from,dist,normal))return false;
    float penetration=0,t=0;
    if(dist<radius)penetration=radius-dist;
    else {
        bool found=false;
        for(unsigned i=1;i<=steps;++i) {
            const float next=float(i)/float(steps);
            if(!sdf.sample(from+delta*next,dist,normal))return false;
            if(dist<=radius) {
                float lo=previous,hi=next;
                for(unsigned j=0;j<8;++j) {
                    const float mid=(lo+hi)*0.5f;
                    if(!sdf.sample(from+delta*mid,dist,normal))return false;
                    if(dist>radius)lo=mid;else hi=mid;
                }
                t=lo;
                if(!sdf.sample(from+delta*hi,dist,normal))return false;
                found=true;break;
            }
            previous=next;
        }
        if(!found)return false;
    }
    const float n=render::length(normal);
    if(!std::isfinite(n)||n<=1e-5f)return false;
    normal=normal*(1.f/n);
    if(penetration==0&&render::dot(delta,normal)>=0)return false;
    to=from+delta*t+normal*(penetration+0.001f);
    const float vn=render::dot(velocity,normal);
    if(vn<0)velocity=(velocity-normal*vn)*(1-dampen)-normal*(vn*bounce);
    return true;
}
}
