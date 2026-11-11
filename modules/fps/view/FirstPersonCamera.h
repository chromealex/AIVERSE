#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
namespace aiverse::fps::view {
struct Look {
    float yaw=0,pitch=0; // turns
    void motion(float dx,float dy,float sensitivity=0.0004f,bool invertY=false) {
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(sensitivity)||sensitivity<0)return;
        // lookAt uses right = cross(forward, up): facing +Z, screen-right is -X.
        yaw=std::fmod(yaw-dx*sensitivity,1.0f);if(yaw<0)yaw+=1.0f;
        pitch=std::clamp(pitch+(invertY?dy:-dy)*sensitivity,-0.248f,0.248f);
    }
    render::RTSCamera camera(render::float3 eye) const {
        constexpr float tau=6.28318530718f;
        auto cp=std::cos(pitch*tau);
        render::float3 forward{std::sin(yaw*tau)*cp,std::sin(pitch*tau),std::cos(yaw*tau)*cp};
        render::RTSCamera result;result.distance=1;result.pitch=-pitch*tau;result.yaw=yaw*tau+tau*0.5f;
        result.target=eye+forward;result.fovY=1.22f;result.zNear=0.03f;result.zFar=1000;return result;
    }
};
}
