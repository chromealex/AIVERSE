// LENS cinematic rails (Catmull-Rom, constant speed, ease, loops) and
// procedural shake (legacy sine + seeded noise). CPU only.
#include "engine/render/CameraRig.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::render;
static void check(bool value,const char* why){
    if(!value)throw std::runtime_error(why);
}
static bool close3(float3 a,float3 b,float eps){return length(a-b)<=eps;}
static CameraRailComponent rail4() {
    CameraRailComponent r;r.count=4;r.duration=3_fx;
    r.points[0]={0_fx,0_fx,0_fx};r.points[1]={4_fx,0_fx,0_fx};
    r.points[2]={4_fx,0_fx,4_fx};r.points[3]={8_fx,2_fx,4_fx};
    return r;
}
int main() {
    // Catmull-Rom passes through every control point at segment boundaries.
    auto r=rail4();r.interpolation=CameraRailInterpolation::CatmullRom;
    check(!cameraRailIsLegacy(r),"spline is not legacy");
    for(int i=0;i<4;++i)
        check(close3(cameraRailPosition(r,double(i)),cameraRailPoint(r,i),1e-4f),"through points");
    // ...and bends away from the straight chord between them.
    const float3 mid=cameraRailPosition(r,1.5);
    check(!close3(mid,float3{4,0,2},1e-3f),"curved between points");
    // Legacy linear rail is unchanged.
    auto linear=rail4();
    check(cameraRailIsLegacy(linear) && close3(cameraRailPosition(linear,1.5),float3{4,0,2},1e-5f),
          "legacy linear midpoint");

    // Constant speed on uneven spacing: equal time = equal distance.
    CameraRailComponent uneven;uneven.count=3;uneven.duration=10_fx;
    uneven.points[1]={1_fx,0_fx,0_fx};uneven.points[2]={10_fx,0_fx,0_fx};
    check(std::fabs(cameraRailPosition(uneven,5).x-1.0f)<1e-4f,"legacy: half time = point 1");
    uneven.constantSpeed=true;
    check(std::fabs(cameraRailPosition(uneven,5).x-5.0f)<1e-3f,"constant speed: half distance");
    check(std::fabs(cameraRailPosition(uneven,2.5).x-2.5f)<1e-3f,"constant speed: quarter");
    check(std::fabs(cameraRailPosition(uneven,10).x-10.0f)<1e-4f,"constant speed: end");

    // Ease in/out: slow start, same midpoint, stationary but oriented ends.
    CameraRailComponent straight;straight.count=2;straight.duration=4_fx;
    straight.points[1]={8_fx,0_fx,0_fx};straight.ease=CameraRailEase::InOut;
    check(cameraRailPosition(straight,0.4).x<0.8f*0.5f,"eased start is slower");
    check(std::fabs(cameraRailPosition(straight,2).x-4.f)<1e-4f,"eased midpoint");
    const auto start=cameraRailSample(straight,0);
    check(start.valid && close3(start.tangent,float3{1,0,0},1e-4f),"tangent defined at rest");

    // Closed spline: continuous across the wrap.
    auto loop=rail4();loop.interpolation=CameraRailInterpolation::CatmullRom;loop.loop=true;
    check(cameraRailSegments(loop)==4,"closed rail has n segments");
    check(close3(cameraRailPosition(loop,2.9999),cameraRailPosition(loop,0),1e-2f),"loop wrap");
    check(close3(cameraRailPosition(loop,3.0+0.75),cameraRailPosition(loop,0.75),1e-4f),"loop period");

    // Degenerate input never produces NaN.
    CameraRailComponent one;one.count=1;one.points[0]={2_fx,3_fx,4_fx};
    one.interpolation=CameraRailInterpolation::CatmullRom;one.constantSpeed=true;
    check(close3(cameraRailPosition(one,1),float3{2,3,4},1e-5f),"single point");
    CameraRailComponent same;same.count=3;same.constantSpeed=true;
    same.interpolation=CameraRailInterpolation::CatmullRom;
    const float3 p=cameraRailPosition(same,1);
    check(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"coincident points");
    check(!cameraRailSample(rail4(),NAN).valid,"non-finite seconds");

    // Rig orientation follows the spline tangent for non-legacy rails.
    World world;
    const Entity view=world.create();
    world.add<WorldTransform>(view,hierarchy_detail::root({}));
    world.add<CameraComponent>(view,CameraComponent{});
    CameraRigComponent rig;rig.mode=CameraRigMode::Rail;rig.smoothing=0_fx;
    rig.trackTarget=false;world.add<CameraRigComponent>(view,rig);
    world.add<CameraRailComponent>(view,straight);
    EcsCameraView snapshot;
    check(resolveEcsCamera(world,view,snapshot),"camera");
    CameraRigEvaluator rigs;
    check(rigs.apply(world,snapshot,0.0,.016f),"rail rig at rest");
    const float3 forward=snapshot.camera.rotation.rotate({0,0,-1});
    check(close3(forward,float3{1,0,0},1e-3f),"looks along the rail at its eased start");

    // Shake: legacy sine is the previous formula; noise is seeded and bounded.
    CameraShakeComponent sine;sine.positionAmplitude=1_fx;sine.rotationAmplitude=Fixed::fromRatio(1,10);
    sine.startTime=1_fx;sine.duration=2_fx;
    const auto s=cameraShakeOffset(sine,1.5);
    const float fade=float(1.0-0.5/2.0),phase=float(0.5*12.0*6.28318530718);
    check(s.active && close3(s.position,float3{std::sin(phase),std::sin(phase*.83f),
          std::cos(phase*1.23f)}*fade,1e-6f),"sine position unchanged");
    check(s.rotation.x==0 && s.rotation.y==0 &&
          std::fabs(s.rotation.z-std::sin(phase*.71f)*float(sine.rotationAmplitude.toDouble())*fade)<1e-6f,"sine roll unchanged");
    check(!cameraShakeOffset(sine,0.5).active && !cameraShakeOffset(sine,3.0).active,"window");
    CameraShakeComponent noise=sine;noise.style=CameraShakeStyle::Noise;noise.seed=7;
    const auto a=cameraShakeOffset(noise,1.3),b=cameraShakeOffset(noise,1.3);
    check(close3(a.position,b.position,0) && close3(a.rotation,b.rotation,0),"deterministic");
    CameraShakeComponent other=noise;other.seed=8;
    check(!close3(cameraShakeOffset(other,1.3).position,a.position,1e-6f),"seed changes pattern");
    bool rotated=false;
    for(int i=0;i<200;++i) {
        const auto o=cameraShakeOffset(noise,1.0+i*0.01);
        check(std::fabs(o.position.x)<=1.f&&std::fabs(o.position.y)<=1.f&&
              std::fabs(o.position.z)<=1.f,"noise bounded by amplitude");
        rotated|=std::fabs(o.rotation.x)>1e-4f&&std::fabs(o.rotation.y)>1e-4f;
        const auto n=cameraShakeOffset(noise,1.0+i*0.01+1e-4);
        check(length(n.position-o.position)<0.05f,"noise is continuous");
    }
    check(rotated,"noise shakes pitch and yaw, not only roll");
    CameraShakeComponent sharp=noise;sharp.fadeExponent=2_fx;
    check(length(cameraShakeOffset(sharp,2.0).position)<length(cameraShakeOffset(noise,2.0).position),
          "higher fade exponent decays faster");
    std::puts("PASS camera_rail_path");
}
