#include <aiverselib/animation/EcsAnimationSystems.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
int main() {
    Model model;model.nodes.resize(1);model.order={0};model.nodes[0].name="root";
    auto graph=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"base","kind":"bind"}],"output":"base"})"));
    World w;Entity e=w.create();
    auto& asset=w.add<AnimationAssetComponent>(e);
    asset.model=&model;asset.graph=graph;
    w.add<AnimationPlayerComponent>(e);
    w.add<AnimationPoseComponent>(e);
    auto& physics=w.add<AnimationPhysicalPoseComponent>(e);
    physics.local.resize(1);physics.local[0].t={4,0,0};
    physics.enabled=true;
    auto& recovery=w.add<AnimationRecoveryComponent>(e);
    stepAnimationPresentation(w,.01f);
    assert(std::abs(w.get<AnimationPoseComponent>(e).local[0].t.x-4.f)<.001f);
    recovery.requested=true;recovery.duration=.5f;
    recovery.from=w.get<AnimationPoseComponent>(e).local;
    physics.enabled=false;
    stepAnimationPresentation(w,.25f);
    assert(std::abs(w.get<AnimationPoseComponent>(e).local[0].t.x-2.f)<.001f);
    stepAnimationPresentation(w,.25f);
    assert(std::abs(w.get<AnimationPoseComponent>(e).local[0].t.x)<.001f);
    return 0;
}
