#pragma once
#include "Math3D.h"
#include <algorithm>
#include <limits>
namespace rts::render {
// Conservative homogeneous clipping: the GL near plane also safely contains Metal's [0,w].
inline bool modelBoxVisible(const mat4& clip, float3 mn, float3 mx) {
    int outside[6]={};
    for(int c=0;c<8;++c){
        float4 p=clip*float4{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z,1};
        outside[0]+=p.x < -p.w;outside[1]+=p.x > p.w;
        outside[2]+=p.y < -p.w;outside[3]+=p.y > p.w;
        outside[4]+=p.z < -p.w;outside[5]+=p.z > p.w;
    }
    for(int n:outside)if(n==8)return false;
    return true;
}
inline float modelPixelDiameter(const mat4& clip,float3 mn,float3 mx,int width,int height) {
    float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for(int c=0;c<8;++c){
        float4 p=clip*float4{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z,1};
        if(p.w<=1e-4f)return std::numeric_limits<float>::infinity(); // near-plane intersection: keep full detail
        x0=std::min(x0,p.x/p.w);x1=std::max(x1,p.x/p.w);y0=std::min(y0,p.y/p.w);y1=std::max(y1,p.y/p.w);
    }
    return std::max((x1-x0)*float(width)*0.5f,(y1-y0)*float(height)*0.5f);
}
}
