// Read-only presentation camera snapshot. No GPU, ECS or document mutation.
#pragma once
#include "engine/render/Math3D.h"
#include <cmath>
#include <string>
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
    std::tuple<int,float,float> rank(render::float3 p,float radius)const{
        const auto eye=camera.eye(),toEye=camera.ortho?eye-camera.target:eye-p;
        const float d2=render::dot(p-eye,p-eye);
        const float target2=render::dot(p-camera.target,p-camera.target);
        const float horizon=render::dot(render::normalize(p),render::normalize(toEye));
        const bool facing=render::dot(eye,eye)<=radius*radius||horizon>=-.08f;
        render::float2 screen{};
        const bool projected=camera.worldToScreen(p,float(width),float(height),screen);
        const bool onScreen=projected&&screen.x>=-32&&screen.y>=-32&&screen.x<=width+32&&screen.y<=height+32;
        return {facing&&onScreen?0:facing?1:2,d2,target2};
    }
};
} // namespace aiverse::studio
