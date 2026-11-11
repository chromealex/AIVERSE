#pragma once
#include <algorithm>
#include <cmath>
namespace aiverse::lib::gameplay::camera {
enum class Mode { Rts,Follow,Orbit,Strategic };
struct Pose { float x=0,z=0,zoom=20,yaw=0,pitch=45; };
struct Limits { float minX=-10000,maxX=10000,minZ=-10000,maxZ=10000,minZoom=1,maxZoom=200,minPitch=5,maxPitch=89; };
inline bool valid(const Pose& p) { return std::isfinite(p.x)&&std::isfinite(p.z)&&std::isfinite(p.zoom)&&std::isfinite(p.yaw)&&std::isfinite(p.pitch); }
inline bool constrain(Pose& p,const Limits& l) {
    const Pose low{l.minX,l.minZ,l.minZoom,0,l.minPitch}, high{l.maxX,l.maxZ,l.maxZoom,0,l.maxPitch};
    if(!valid(p)||!valid(low)||!valid(high)||l.minX>l.maxX||l.minZ>l.maxZ||l.minZoom<=0||l.minZoom>l.maxZoom||l.minPitch>l.maxPitch) return false;
    p.x=std::clamp(p.x,l.minX,l.maxX); p.z=std::clamp(p.z,l.minZ,l.maxZ); p.zoom=std::clamp(p.zoom,l.minZoom,l.maxZoom);
    p.pitch=std::clamp(p.pitch,l.minPitch,l.maxPitch); p.yaw=std::remainder(p.yaw,360.0f); return true;
}
inline bool advance(Pose& current,Pose target,const Limits& limits,float delta,float response) {
    if(!valid(current)||!std::isfinite(delta)||!std::isfinite(response)||delta<0||response<0||!constrain(target,limits)) return false;
    float t=1-std::exp(-response*delta); auto result=current;
    result.x+=(target.x-result.x)*t; result.z+=(target.z-result.z)*t; result.zoom+=(target.zoom-result.zoom)*t;
    result.yaw+=std::remainder(target.yaw-result.yaw,360.0f)*t;result.pitch+=(target.pitch-result.pitch)*t;
    if(!constrain(result,limits)) return false;
    current=result;return true;
}
// Pan/follow/zoom compute the target; apply it to the host camera, never the simulation.
inline bool pan(Pose& target,float x,float z,const Limits& limits) { auto next=target;next.x+=x;next.z+=z;if(!constrain(next,limits))return false;target=next;return true; }
struct Input {float panX=0,panZ=0,yaw=0,pitch=0,zoom=0,followX=0,followZ=0;};
inline bool updateTarget(Pose& target,Mode mode,const Input& input,const Limits& limits) {
    auto next=target;
    if(mode==Mode::Follow){next.x=input.followX;next.z=input.followZ;}
    else {next.x+=input.panX;next.z+=input.panZ;}
    next.yaw+=input.yaw;next.pitch+=input.pitch;next.zoom+=input.zoom;
    if(!constrain(next,limits))return false;target=next;return true;
}
inline bool strategicIcons(const Pose& p,float threshold){return p.zoom>=threshold;}
}
