#include "engine/animation/MotionWeaveSourceBridge.h"
#include "engine/animation/MotionWeave.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::motionweave;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main() {
    render::Model model;model.nodes.resize(1);
    model.nodes[0].name="root";model.order={0};
    auto graph=render::AnimationGraph::compile(model,Json::parse(R"({
        "parameters":{"speed":0,"production":0},
        "nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));
    World p;
    MotionWeaveSourceBridge bridge;
    AnimationSourceFrame a;
    a.sourceId=123;a.generation=1;a.model=&model;a.graph=graph;
    a.inputs.set("speed",2.f);
    AnimationSourceFrame b=a;b.sourceId=987;
    b.inputs.set("production",.5f);
    const std::vector<AnimationSourceFrame> initial={a,b};
    auto stats=bridge.sync(p,initial);
    AIVERSE_TEST_CHECK(stats.created==2 && stats.updated==0);
    const Entity e=bridge.find(123),other=bridge.find(987);
    AIVERSE_TEST_CHECK(p.alive(e)&&p.alive(other));
    MotionWeave::update(p,.016f);
    AIVERSE_TEST_CHECK(p.get<AnimationPoseComponent>(e).valid);
    AIVERSE_TEST_CHECK(p.get<AnimationInputsComponent>(other).overrides.values.at("production")==.5f);
    // Stable identity updates inputs, but never restarts the player.
    a.inputs.set("speed",3.f);
    std::vector<AnimationSourceFrame> frames={a,b};
    stats=bridge.sync(p,frames);
    AIVERSE_TEST_CHECK(stats.updated==2 && stats.restarted==0 && bridge.find(123)==e);
    // Generation changes trigger a visual-only restart.
    frames[0].generation=2;
    stats=bridge.sync(p,frames);
    AIVERSE_TEST_CHECK(stats.restarted==1);
    AIVERSE_TEST_CHECK(p.get<AnimationPlayerComponent>(e).restart);
    // Duplicate IDs and invalid models fail before mutation.
    bool threw=false;
    try {std::vector<AnimationSourceFrame> duplicates={frames[0],frames[0]};
         bridge.sync(p,duplicates,true);
    }catch(const std::invalid_argument&){threw=true;}
    AIVERSE_TEST_CHECK(threw && bridge.size()==2);
    stats=bridge.sync(p,{frames.data(),1});
    AIVERSE_TEST_CHECK(stats.removed==1 && !p.alive(other));
    bridge.clear(p);
    AIVERSE_TEST_CHECK(bridge.size()==0 && !p.alive(e));
}
