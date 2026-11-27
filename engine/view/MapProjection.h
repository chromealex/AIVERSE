#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace aiverse::view {
// Shared planar map projection. Rendering, overlays and pointer input use the
// same rectangle; no gameplay type or map format is involved.
struct MapProjection {
    float left=0, top=0, right=1, bottom=1;
    bool set(float x0,float z0,float x1,float z1) {
        if(!std::isfinite(x0)||!std::isfinite(z0)||!std::isfinite(x1)||!std::isfinite(z1)||x1<=x0||z1<=z0)return false;
        left=x0;top=z0;right=x1;bottom=z1;return true;
    }
    float width() const {return right-left;}
    float height() const {return bottom-top;}
    render::float2 uv(float x,float z) const {return {(x-left)/width(),(z-top)/height()};}
    render::float3 point(float u,float v) const {return {left+u*width(),0,top+v*height()};}
    bool intersects(float x,float z,float radius=0) const {
        return x+radius>=left&&x-radius<right&&z+radius>=top&&z-radius<bottom;
    }
    bool clipTo(const MapProjection& outer) {
        return set(std::max(left,outer.left),std::max(top,outer.top),
                   std::min(right,outer.right),std::min(bottom,outer.bottom));
    }
    static std::array<render::float3,4> rectangularFrame(const render::float3 (&quad)[4]) {
        float x0=quad[0].x,x1=x0,z0=quad[0].z,z1=z0;
        for(const auto& p:quad){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        return {{{x0,0,z0},{x1,0,z0},{x1,0,z1},{x0,0,z1}}};
    }
};
} // namespace aiverse::view
