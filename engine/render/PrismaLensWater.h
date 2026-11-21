// PRISMA presentation policy: wet lens is independent from underwater optics.
#pragma once
#include <algorithm>
#include <cmath>
namespace aiverse::render {
struct PrismaLensWaterState {
    float wetness=0,age=0;
    bool submerged=false;
};
inline PrismaLensWaterState advancePrismaLensWater(
        PrismaLensWaterState state,bool submerged,float seconds,float drySeconds) {
    const float dt=std::isfinite(seconds)?std::clamp(seconds,0.f,.25f):0.f;
    const float dry=std::isfinite(drySeconds)?std::clamp(drySeconds,.1f,30.f):4.f;
    if(submerged){state={1,0,true};return state;}
    if(state.submerged){state.age=0;state.wetness=1;state.submerged=false;}
    if(state.wetness>0){state.age+=dt;state.wetness=std::clamp(1-state.age/dry,0.f,1.f);}
    if(state.wetness<=0)state={};
    return state;
}
}
