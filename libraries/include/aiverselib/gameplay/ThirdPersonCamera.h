#pragma once
// follow-camera: third-person camera boom for the host camera (presentation only, float math, never fed back into the
// simulation). Orbit yaw/pitch around a pivot above the target, shoulder offset with side swap, obstacle avoidance by a
// sphere sweep supplied by the owner (pull in immediately, ease back out), auto-recenter behind the target's heading
// after idle time, and a controller stack so gameplay can push temporary camera profiles (aim, vehicle, cinematic) and
// pop them with a blend. Angles in degrees, yaw 0 looks along +Z, +X is right, +Y is up.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
namespace aiverse::lib::gameplay::camera {
struct V3 {float x=0,y=0,z=0;};
inline V3 operator+(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float length(V3 a){return std::sqrt(dot(a,a));}
inline bool finite(V3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}

struct BoomProfile {
    float distance=4.5f,minDistance=0.6f,height=1.6f,shoulder=0.6f,probeRadius=0.25f;
    float minPitch=-60,maxPitch=70,pullInResponse=40,pushOutResponse=4,angleResponse=18,recenterAfter=2.5f,recenterResponse=2,fov=70;
};
inline bool valid(const BoomProfile& p){
    for(float v:{p.distance,p.minDistance,p.height,p.shoulder,p.probeRadius,p.minPitch,p.maxPitch,p.pullInResponse,p.pushOutResponse,p.angleResponse,p.recenterAfter,p.recenterResponse,p.fov})
        if(!std::isfinite(v))return false;
    return p.distance>0&&p.minDistance>0&&p.minDistance<=p.distance&&p.probeRadius>=0&&p.minPitch<p.maxPitch&&p.minPitch>=-89&&p.maxPitch<=89&&p.pullInResponse>0
        &&p.pushOutResponse>0&&p.angleResponse>0&&p.recenterAfter>=0&&p.recenterResponse>=0&&p.fov>1&&p.fov<179;
}
// Owner obstacle query: distance along `dir` (unit) from `origin` at which a sphere of `radius` first touches geometry,
// or `maxDistance` when nothing is hit. Ignore the followed character itself.
using SweepQuery=std::function<float(V3 origin,V3 dir,float maxDistance,float radius)>;
struct BoomInput {V3 target{};float targetYaw=0,lookYaw=0,lookPitch=0,dt=0;bool swapShoulder=false;};
struct BoomState {float yaw=0,pitch=10,distance=4.5f,idle=0,shoulderSign=1,blend=1;BoomProfile from{};bool initialized=false;};
struct BoomPose {V3 eye{},pivot{},lookAt{};float distance=0,fov=70;bool occluded=false;};

inline float wrapDegrees(float a){return std::remainder(a,360.0f);}
inline V3 forwardFrom(float yaw,float pitch){float y=yaw*3.14159265f/180,p=pitch*3.14159265f/180;return {std::sin(y)*std::cos(p),-std::sin(p),std::cos(y)*std::cos(p)};}
inline BoomProfile lerp(const BoomProfile& a,const BoomProfile& b,float t){
    auto m=[t](float x,float y){return x+(y-x)*t;};BoomProfile r=b;
    r.distance=m(a.distance,b.distance);r.minDistance=m(a.minDistance,b.minDistance);r.height=m(a.height,b.height);r.shoulder=m(a.shoulder,b.shoulder);
    r.probeRadius=m(a.probeRadius,b.probeRadius);r.fov=m(a.fov,b.fov);return r;
}
// Controller stack: the top profile is active; push/pop start a blend of blendSeconds from the previous effective profile.
struct ProfileStack {std::vector<std::pair<uint32_t,BoomProfile>> entries;float blendSeconds=0.25f;};
inline BoomProfile effective(const BoomState& s,const ProfileStack& stack,const BoomProfile& base){
    const auto& top=stack.entries.empty()?base:stack.entries.back().second;return s.blend>=1?top:lerp(s.from,top,s.blend);}
inline bool pushProfile(ProfileStack& stack,BoomState& s,const BoomProfile& base,uint32_t id,const BoomProfile& p){
    if(!valid(p)||id==0||stack.entries.size()>=16)return false;for(auto& [k,v]:stack.entries)if(k==id)return false;
    s.from=effective(s,stack,base);s.blend=0;stack.entries.push_back({id,p});return true;}
inline bool popProfile(ProfileStack& stack,BoomState& s,const BoomProfile& base,uint32_t id){
    auto it=std::find_if(stack.entries.begin(),stack.entries.end(),[id](auto& e){return e.first==id;});if(it==stack.entries.end())return false;
    s.from=effective(s,stack,base);s.blend=0;stack.entries.erase(it);return true;}

// One presentation frame. Returns false (state untouched) for invalid input.
inline bool updateBoom(BoomState& state,const BoomProfile& base,const ProfileStack& stack,const BoomInput& in,const SweepQuery& sweep,BoomPose& out){
    if(!valid(base)||!finite(in.target)||!std::isfinite(in.dt)||in.dt<0||in.dt>1||!std::isfinite(in.lookYaw)||!std::isfinite(in.lookPitch)||!std::isfinite(in.targetYaw))return false;
    for(auto& [id,p]:stack.entries)if(!valid(p))return false;
    auto s=state;
    if(!s.initialized){s.yaw=in.targetYaw;s.pitch=10;s.distance=base.distance;s.from=base;s.initialized=true;}
    if(stack.blendSeconds>0)s.blend=std::min(1.0f,s.blend+in.dt/stack.blendSeconds);else s.blend=1;
    auto p=effective(s,stack,base);
    if(in.swapShoulder)s.shoulderSign=-s.shoulderSign;
    bool looking=std::abs(in.lookYaw)>1e-4f||std::abs(in.lookPitch)>1e-4f;
    s.idle=looking?0:s.idle+in.dt;
    s.yaw=wrapDegrees(s.yaw+in.lookYaw);s.pitch=std::clamp(s.pitch+in.lookPitch,p.minPitch,p.maxPitch);
    if(!looking&&s.idle>=p.recenterAfter&&p.recenterResponse>0){float t=1-std::exp(-p.recenterResponse*in.dt);s.yaw=wrapDegrees(s.yaw+wrapDegrees(in.targetYaw-s.yaw)*t);}
    V3 pivot=in.target+V3{0,p.height,0};
    V3 fwd=forwardFrom(s.yaw,s.pitch);float yr=s.yaw*3.14159265f/180;V3 right{std::cos(yr),0,-std::sin(yr)};
    V3 shoulderPoint=pivot+right*(p.shoulder*s.shoulderSign);
    // Probe from the pivot to the shoulder point, then backwards along the view direction.
    float side=p.shoulder>0?length(shoulderPoint-pivot):0;V3 sideDir=side>0?(shoulderPoint-pivot)*(1/side):V3{};
    float sideHit=side>0&&sweep?std::clamp(sweep(pivot,sideDir,side,p.probeRadius),0.0f,side):side;
    V3 origin=pivot+sideDir*sideHit;V3 back=fwd*-1;
    float hit=sweep?sweep(origin,back,p.distance,p.probeRadius):p.distance;
    if(!std::isfinite(hit))return false;
    float wanted=std::clamp(hit,p.minDistance,p.distance);bool occluded=hit<p.distance-1e-3f||sideHit<side-1e-3f;
    float response=wanted<s.distance?p.pullInResponse:p.pushOutResponse;
    s.distance+= (wanted-s.distance)*(1-std::exp(-response*in.dt));
    if(s.distance>std::max(hit,p.minDistance))s.distance=std::max(hit,p.minDistance);            // never stay inside geometry
    out.pivot=pivot;out.eye=origin+back*s.distance;out.lookAt=origin+fwd*10.0f;out.distance=s.distance;out.fov=p.fov;out.occluded=occluded;
    state=s;return true;
}
}
