// AIVERSE LENS: cinematic rail paths and procedural shake (presentation only).
// Pure functions of authored ECS data and absolute presentation seconds, so
// rails and shakes can be seeked/scrubbed deterministically. No ECS writes.
#pragma once
#include "Math3D.h"
#include "../sim/CameraRigComponents.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

inline float3 cameraRailPoint(const CameraRailComponent& rail,int i) {
    const Vec3 p=rail.points[size_t(i)];
    return {float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};
}
inline int cameraRailCount(const CameraRailComponent& rail) {
    return std::min<int>(rail.count,int(rail.points.size()));
}
// Number of spline segments: open rail n-1, closed (loop) rail n.
inline int cameraRailSegments(const CameraRailComponent& rail) {
    const int n=cameraRailCount(rail);
    if(n<2)return 0;
    return rail.loop&&n>2?n:n-1;
}
inline float3 cameraRailControl(const CameraRailComponent& rail,int i) {
    const int n=cameraRailCount(rail);
    if(rail.loop&&n>2)return cameraRailPoint(rail,((i%n)+n)%n);
    if(i<0) { // reflect the first point so the end tangent follows the rail
        const float3 a=cameraRailPoint(rail,0),b=cameraRailPoint(rail,1);
        return a+(a-b);
    }
    if(i>=n) {
        const float3 a=cameraRailPoint(rail,n-1),b=cameraRailPoint(rail,n-2);
        return a+(a-b);
    }
    return cameraRailPoint(rail,i);
}
// Centripetal Catmull-Rom (alpha 0.5): no cusps or self-loops on uneven spacing.
inline float3 cameraCatmullRom(float3 p0,float3 p1,float3 p2,float3 p3,float u) {
    const auto knot=[](float3 a,float3 b){
        return std::max(std::sqrt(length(b-a)),1.e-4f);
    };
    const float t0=0,t1=t0+knot(p0,p1),t2=t1+knot(p1,p2),t3=t2+knot(p2,p3);
    const float t=t1+(t2-t1)*u;
    const float3 a1=p0*((t1-t)/(t1-t0))+p1*((t-t0)/(t1-t0));
    const float3 a2=p1*((t2-t)/(t2-t1))+p2*((t-t1)/(t2-t1));
    const float3 a3=p2*((t3-t)/(t3-t2))+p3*((t-t2)/(t3-t2));
    const float3 b1=a1*((t2-t)/(t2-t0))+a2*((t-t0)/(t2-t0));
    const float3 b2=a2*((t3-t)/(t3-t1))+a3*((t-t1)/(t3-t1));
    return b1*((t2-t)/(t2-t1))+b2*((t-t1)/(t2-t1));
}
// Position at path parameter s in [0, segments].
inline float3 cameraRailAtParameter(const CameraRailComponent& rail,double s) {
    const int segments=cameraRailSegments(rail);
    if(segments<=0)return cameraRailCount(rail)?cameraRailPoint(rail,0):float3{};
    s=std::clamp(s,0.0,double(segments));
    const int i=std::min(int(s),segments-1);
    const float u=float(s-i);
    if(rail.interpolation==CameraRailInterpolation::CatmullRom)
        return cameraCatmullRom(cameraRailControl(rail,i-1),cameraRailControl(rail,i),
                                cameraRailControl(rail,i+1),cameraRailControl(rail,i+2),u);
    return lerp(cameraRailControl(rail,i),cameraRailControl(rail,i+1),u);
}
// Normalized rail progress in [0,1] for absolute seconds (loop wraps).
inline double cameraRailProgress(const CameraRailComponent& rail,double seconds) {
    const double duration=std::max(0.001,rail.duration.toDouble());
    double t=seconds/duration;
    if(rail.loop)t=t-std::floor(t);
    else t=std::clamp(t,0.,1.);
    if(rail.ease==CameraRailEase::InOut)t=t*t*(3.0-2.0*t);
    return t;
}
// Arc-length table: 16 samples per segment, at most 8 segments.
struct CameraRailArcTable {
    static constexpr int kPerSegment=16;
    std::array<float,8*kPerSegment+1> distance{};
    int samples=0;
    float total=0;
};
inline CameraRailArcTable cameraRailArcTable(const CameraRailComponent& rail) {
    CameraRailArcTable table;
    const int segments=cameraRailSegments(rail);
    if(segments<=0)return table;
    table.samples=segments*CameraRailArcTable::kPerSegment+1;
    float3 previous=cameraRailAtParameter(rail,0);
    for(int k=1;k<table.samples;++k) {
        const float3 p=cameraRailAtParameter(rail,double(k)/CameraRailArcTable::kPerSegment);
        table.distance[size_t(k)]=table.distance[size_t(k-1)]+length(p-previous);
        previous=p;
    }
    table.total=table.distance[size_t(table.samples-1)];
    return table;
}
// Path parameter for normalized progress, honouring constantSpeed.
inline double cameraRailParameter(const CameraRailComponent& rail,double progress) {
    const int segments=cameraRailSegments(rail);
    if(segments<=0)return 0;
    if(!rail.constantSpeed)return progress*segments;
    const auto table=cameraRailArcTable(rail);
    if(!(table.total>1.e-6f))return progress*segments;
    const float goal=float(progress)*table.total;
    const auto begin=table.distance.begin(),end=begin+table.samples;
    const auto it=std::lower_bound(begin,end,goal);
    const int k=std::clamp(int(it-begin),1,table.samples-1);
    const float d0=table.distance[size_t(k-1)],d1=table.distance[size_t(k)];
    const float f=d1>d0?(goal-d0)/(d1-d0):0.f;
    return (double(k-1)+f)/CameraRailArcTable::kPerSegment;
}
struct CameraRailSample { float3 position{}; float3 tangent{}; bool valid=false; };
// Position plus unit travel direction (from the path, not from time, so it
// stays defined while an eased rail is momentarily stationary).
inline CameraRailSample cameraRailSample(const CameraRailComponent& rail,double seconds) {
    CameraRailSample out;
    const int n=cameraRailCount(rail);
    if(n<=0||!std::isfinite(seconds))return out;
    out.valid=true;
    if(n==1){out.position=cameraRailPoint(rail,0);return out;}
    const double s=cameraRailParameter(rail,cameraRailProgress(rail,seconds));
    out.position=cameraRailAtParameter(rail,s);
    const int segments=cameraRailSegments(rail);
    const double h=1.0/64;
    const double a=std::max(0.0,s-h),b=std::min(double(segments),s+h);
    const float3 d=cameraRailAtParameter(rail,b)-cameraRailAtParameter(rail,a);
    if(length(d)>1.e-6f)out.tangent=normalize(d);
    return out;
}
// True when the rail uses only the original linear behaviour.
inline bool cameraRailIsLegacy(const CameraRailComponent& rail) {
    return rail.interpolation==CameraRailInterpolation::Linear&&
           !rail.constantSpeed&&rail.ease==CameraRailEase::None;
}

// ---- shake
// Smooth 1D value noise in [-1,1], seeded per channel (quintic fade).
inline float cameraShakeNoise(uint32_t seed,uint32_t channel,double x) {
    const auto hash=[](uint32_t v){
        v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;
    };
    const double fl=std::floor(x);
    const int64_t i=int64_t(fl);
    const float f=float(x-fl);
    const auto value=[&](int64_t k){
        const uint32_t h=hash(seed*0x9e3779b9u^channel*0x85ebca6bu^uint32_t(k)^uint32_t(uint64_t(k)>>32));
        return float(h)/4294967295.f*2.f-1.f;
    };
    const float u=f*f*f*(f*(f*6.f-15.f)+10.f);
    return value(i)+(value(i+1)-value(i))*u;
}
struct CameraShakeOffset { float3 position{}; float3 rotation{}; bool active=false; };
// Offset at absolute seconds: position in metres, rotation as pitch/yaw/roll radians.
inline CameraShakeOffset cameraShakeOffset(const CameraShakeComponent& shake,double seconds) {
    CameraShakeOffset out;
    const double age=seconds-shake.startTime.toDouble();
    const double duration=shake.duration.toDouble();
    if(!std::isfinite(age)||!(duration>0)||age<0||age>=duration)return out;
    const double k=std::max(0.0,shake.fadeExponent.toDouble());
    const float fade=float(std::pow(1.0-age/duration,k));
    const float move=float(shake.positionAmplitude.toDouble())*fade;
    const float turn=float(shake.rotationAmplitude.toDouble())*fade;
    out.active=true;
    if(shake.style==CameraShakeStyle::Noise) {
        const double x=age*shake.frequency.toDouble();
        const uint32_t s=shake.seed;
        out.position={cameraShakeNoise(s,0,x),cameraShakeNoise(s,1,x),cameraShakeNoise(s,2,x)};
        out.position=out.position*move;
        out.rotation={cameraShakeNoise(s,3,x)*turn,cameraShakeNoise(s,4,x)*turn,
                      cameraShakeNoise(s,5,x)*turn};
        return out;
    }
    // Original sine shake (bit-for-bit the previous formula when k = 1).
    const float phase=float(age*shake.frequency.toDouble()*6.28318530718);
    out.position=float3{std::sin(phase),std::sin(phase*.83f),std::cos(phase*1.23f)}*move;
    out.rotation={0,0,std::sin(phase*.71f)*turn};
    return out;
}
} // namespace aiverse::render
