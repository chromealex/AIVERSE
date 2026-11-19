// CPU-only cameras: rig target, rail, shake, physical lens and director.
#include "engine/render/CameraDirector.h"
#include <cmath>
#include <stdexcept>
using namespace aiverse;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main() {
    World world;
    auto focus=world.create(),view=world.create();
    LocalTransform at{};at.position={10_fx,2_fx,5_fx};
    world.add<WorldTransform>(focus,hierarchy_detail::root(at));
    world.add<WorldTransform>(view,hierarchy_detail::root({}));
    world.add<CameraComponent>(view,CameraComponent{});
    CameraRigComponent follow;follow.mode=CameraRigMode::Follow;
    follow.target=focus;follow.offset={0_fx,2_fx,5_fx};follow.smoothing=0_fx;
    world.add<CameraRigComponent>(view,follow);
    render::EcsCameraView snapshot;
    check(render::resolveEcsCamera(world,view,snapshot),"initial camera");
    render::CameraRigEvaluator rigs;
    check(rigs.apply(world,snapshot,1.0,.016f),"follow rig");
    check(std::fabs(snapshot.camera.eye().x-10.f)<0.001f,"follow x");
    check(std::fabs(snapshot.camera.eye().y-4.f)<0.001f,"follow y");
    CameraLensComponent lens;lens.physicalFov=true;
    world.add<CameraLensComponent>(view,lens);
    check(rigs.apply(world,snapshot,1.0,.016f),"physical lens");
    check(snapshot.camera.fovY>0.4f && snapshot.camera.fovY<0.6f,"50mm/24mm FOV");
    auto rail=CameraRailComponent{};rail.count=2;rail.points[0]={0_fx,0_fx,0_fx};rail.points[1]={20_fx,0_fx,0_fx};
    rail.duration=10_fx;
    check(std::fabs(render::cameraRailPosition(rail,5.0).x-10.f)<.001f,"rail halfway");
    auto other=world.create();world.add<WorldTransform>(other,hierarchy_detail::root({}));
    CameraComponent upper;upper.priority=15;world.add<CameraComponent>(other,upper);
    render::EcsCameraView second;
    check(render::resolveEcsCamera(world,other,second),"second camera");
    std::vector<render::EcsCameraView> candidates{snapshot,second};
    render::CameraDirector director;
    auto frame=director.evaluate(candidates,.016f);
    check(frame.valid && frame.source==other && frame.cut,"highest priority screen camera");
    world.get<CameraComponent>(other).priority=-1;
    second.priority=-1;
    frame=director.evaluate(candidates,.016f);
    check(frame.valid && frame.source==view && frame.blendWeight<1.f,"blend on camera switch");
    director.cut();
    frame=director.evaluate(candidates,.2f);
    check(frame.valid && frame.blendWeight==1.f,"explicit cut");
}
