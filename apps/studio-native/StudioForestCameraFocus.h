// Read-only presentation camera snapshot. No GPU, ECS or document mutation.
#pragma once
#include "engine/render/Math3D.h"
#include <cmath>
#include <string>
#include <optional>
#include <limits>
#include <tuple>
namespace aiverse::studio {
struct StudioForestCameraFocus {
    std::string body;
    render::Camera camera;
    int width=0,height=0;
    bool valid()const{
        const auto e=camera.eye(),t=camera.target;
        const auto finite=[](render::float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        return !body.empty()&&width>=2&&height>=2&&width<=32768&&height<=32768&&
            finite(e)&&finite(t)&&std::isfinite(camera.distance)&&camera.distance>0&&
            std::isfinite(camera.fovY)&&camera.fovY>0&&camera.fovY<3.14f&&
            render::dot(e-t,e-t)>1e-6f;
    }
    // Conservative ranking only: not visibility culling. Behind-horizon or
    // offscreen plants may still be included if spare capacity remains.
    render::mat4 viewProjection()const{return camera.proj(float(width)/float(height),false)*camera.view();}
    std::tuple<int,float,float> rank(render::float3 p,float radius,const render::mat4& clip)const{
        const auto eye=camera.eye(),toEye=camera.ortho?eye-camera.target:eye-p;
        const float d2=render::dot(p-eye,p-eye);
        const float target2=render::dot(p-camera.target,p-camera.target);
        const float horizon=render::dot(render::normalize(p),render::normalize(toEye));
        const bool facing=render::dot(eye,eye)<=radius*radius||horizon>=-.08f;
        const auto projected=clip*render::float4{p.x,p.y,p.z,1.f};
        const bool positive=projected.w>.0001f;
        const float x=positive?(projected.x/projected.w*.5f+.5f)*width:0.f;
        const float y=positive?(1.f-(projected.y/projected.w*.5f+.5f))*height:0.f;
        const bool onScreen=positive&&x>=-32&&y>=-32&&x<=width+32&&y<=height+32;
        if(!std::isfinite(d2)||!std::isfinite(target2))return {2,std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
        return {facing&&onScreen?0:facing?1:2,d2,target2};
    }
};
} // namespace aiverse::studio
