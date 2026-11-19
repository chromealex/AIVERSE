// Four side-plane frustum culling shared by all View scene providers.
// Plane lengths are cached once per camera, not per visible sphere.
#pragma once
#include "engine/render/Math3D.h"
#include <array>
#include <cmath>
#include <cstddef>
namespace aiverse::view {
struct SideFrustum {
    std::array<render::float4,4> planes{};
    std::array<float,4> normalLengths{};
    static SideFrustum fromViewProjection(const render::mat4& vp,
                                          bool normalized=false) {
        SideFrustum result;
        for(int i=0;i<4;++i) {
            const int row=i/2;
            const float sign=i%2?-1.f:1.f;
            auto& plane=result.planes[std::size_t(i)];
            plane={vp.at(3,0)+sign*vp.at(row,0),
                   vp.at(3,1)+sign*vp.at(row,1),
                   vp.at(3,2)+sign*vp.at(row,2),
                   vp.at(3,3)+sign*vp.at(row,3)};
            const float length=std::sqrt(plane.x*plane.x+
                plane.y*plane.y+plane.z*plane.z);
            if(normalized) {
                if(std::isfinite(length)&&length>1.e-12f) {
                    plane={plane.x/length,plane.y/length,
                           plane.z/length,plane.w/length};
                    result.normalLengths[std::size_t(i)]=1.f;
                } else {plane={0,0,0,0};}
            } else {
                result.normalLengths[std::size_t(i)]=
                    std::isfinite(length)?length:0.f;
            }
        }
        return result;
    }
    bool visibleAABB(render::float3 mn,render::float3 mx) const {
        for(const auto& plane:planes) {
            const render::float3 q{plane.x>=0?mx.x:mn.x,
                plane.y>=0?mx.y:mn.y,plane.z>=0?mx.z:mn.z};
            if(plane.x*q.x+plane.y*q.y+plane.z*q.z+plane.w<0.f)
                return false;
        }
        return true;
    }
    bool visibleSphere(render::float3 c,float radius) const {
        if(!std::isfinite(radius)||radius<0.f)return false;
        for(std::size_t i=0;i<planes.size();++i) {
            const auto& plane=planes[i];
            if(plane.x*c.x+plane.y*c.y+plane.z*c.z+plane.w<
                -radius*normalLengths[i])return false;
        }
        return true;
    }
};
} // namespace aiverse::view
