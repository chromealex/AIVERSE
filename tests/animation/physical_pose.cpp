#include "engine/animation/PhysicalPoseBlend.h"
#include "engine/animation/EcsAnimationSystems.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
using namespace aiverse::lib::animation;
using namespace aiverse::render;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
static void check(bool b,const char* reason){
    if(!b){std::fprintf(stderr,"physical-pose: %s\n",reason);std::exit(1);}
}
int main(){
    Pose animated(2),simulated(2);
    animated[0].t={0,0,0};animated[1].t={0,0,0};
    simulated[0].t={2,0,0};simulated[1].t={0,2,0};
    simulated[0].r=quat::axisAngle({0,1,0},3.14159265f);
    simulated[0].w[0]=1.f;
    const float mask[]={.5f,0.f};
    check(blendPhysicalPose(animated,simulated,mask,1.f),"masked blend");
    check(std::abs(animated[0].t.x-1.f)<.001f,"half-weight root");
    check(std::abs(animated[1].t.y)<.001f,"excluded bone");
    check(std::abs(animated[0].w[0]-.5f)<.001f,"morph interpolation");
    const auto before=animated;
    simulated[1].t.y=NAN;
    check(!blendPhysicalPose(animated,simulated),"invalid external pose rejected");
    check(animated[0].t.x==before[0].t.x,"invalid pose must not partially modify output");
    // Actual ECS composition from an unanimated bind graph to physics pose.
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    auto graph=AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));
    World present;
    Entity e=present.create();
    auto& asset=present.add<AnimationAssetComponent>(e);
    asset.model=&model;asset.graph=graph;
    present.add<AnimationPlayerComponent>(e);
    present.add<AnimationPoseComponent>(e);
    auto& physical=present.add<AnimationPhysicalPoseComponent>(e);
    physical.enabled=true;physical.weight=.5f;
    physical.local.resize(1);physical.local[0].t={4,0,0};
    stepAnimationPresentation(present,.016f);
    check(std::abs(present.get<AnimationPoseComponent>(e).local[0].t.x-2.f)<.001f,
          "physical ECS overlay");
    physical.local[0].t.x=6.f;
    stepAnimationPresentation(present,.016f);
    check(std::abs(present.get<AnimationPoseComponent>(e).local[0].t.x-3.f)<.001f,
          "fresh rigidbody pose sampled every frame");
    std::puts("physical pose blending OK");
}
