// The public MotionWeave header/CMake target must work for native hosts.
#include <aiverselib/motionweave/MotionWeave.h>
#include "../AlwaysCheck.h"
#include <cmath>
#include <string>
using namespace aiverse;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main(){
    render::Model model;
    model.nodes.resize(1);
    model.nodes[0].name="root";
    model.order={0};
    auto graph=render::AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],
        "output":"bind"
    })"));
    World presentation;
    const Entity entity=presentation.create();
    auto& asset=presentation.add<motionweave::AnimationAssetComponent>(entity);
    asset.model=&model;
    asset.graph=std::move(graph);
    presentation.add<motionweave::AnimationPlayerComponent>(entity);
    presentation.add<motionweave::AnimationPoseComponent>(entity);
    motionweave::MotionWeave::update(presentation,1.0f/60);
    const auto& pose=presentation.get<motionweave::AnimationPoseComponent>(entity);
    AIVERSE_TEST_CHECK(pose.valid && pose.local.size()==1 && pose.matrices.size()==1);
    // Regression: the persistent MotionWeave scheduler must use public
    // read-only World accessors. Optional Inputs/WorkHint/Budget components
    // are absent here; their pools must not be created by a read.
    motionweave::MotionWeaveScheduler scheduler(0);
    const size_t poolsBefore=presentation.poolCount();
    const auto stats=motionweave::MotionWeave::update(
        presentation,1.0f/60.0f,scheduler);
    if(stats.eligible!=1 || stats.serialEntities!=0 ||
       presentation.poolCount()!=poolsBefore ||
       !presentation.get<motionweave::AnimationPoseComponent>(entity).valid)
        return 1;
    static_assert(std::is_same_v<motionweave::AnimationPoseComponent,
        lib::animation::AnimationPoseComponent>);
    static_assert(std::is_same_v<lib::motionweave::AnimationPlayerComponent,
        lib::animation::AnimationPlayerComponent>);
    return 0;
}
