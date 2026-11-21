#pragma once
// A camera behind and above a vehicle (any client, any project): `pos` in local render space, `yawTurns` the heading
// in the vehicle controllers' convention (0 faces +Z, a quarter turn faces +X). Presentation only.
#include "engine/render/Math3D.h"
#include <cmath>
namespace aiverse::render {
inline RTSCamera chaseCamera(float3 pos,float yawTurns,float distance=11.f,float height=1.4f,float pitch=0.26f){
    constexpr float tau=6.28318530718f;
    float3 forward{std::sin(yawTurns*tau),0,std::cos(yawTurns*tau)};
    RTSCamera c;c.target=pos+forward*2.5f+float3{0,height,0};c.distance=distance;c.pitch=pitch;c.yaw=yawTurns*tau+tau*0.5f;
    c.fovY=1.05f;c.zNear=0.1f;c.zFar=1500;return c;
}
// Shortest-arc interpolation of a heading in turns (between two snapshots).
inline float lerpTurns(float a,float b,float t){float d=std::fmod(b-a+1.5f,1.0f)-0.5f;return a+d*t;}
}
