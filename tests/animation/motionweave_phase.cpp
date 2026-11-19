#include <aiverselib/motionweave/MotionWeave.h>
#include <aiverselib/animation/AnimationCrowdSystems.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::motionweave;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main() {
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimClip c;c.duration=2.f;c.name="Walk";
    AnimChannel track;track.node=0;track.times={0,2};track.values={0,0,0,2,0,0};
    c.channels.push_back(track);model.clips.push_back(c);
    auto graph=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"move","kind":"clip","clip":"Walk",
                  "notifies":[{"time":0,"name":"step"}]}],"output":"move"})"));
    AnimationGraphInstance inst;inst.reset(graph);
    assert(inst.seekClipNode("move",.75f));
    assert(inst.clipTime("move") && std::abs(*inst.clipTime("move")-.75f)<.001f);
    inst.update(.01f);
    assert(inst.notifies().empty());
    Animator legacy;legacy.setModel(&model);
    legacy.play(0,0,{});
    assert(legacy.seek(0,.75f));
    assert(std::abs(legacy.time(0)-.75f)<.001f);
    World world;Entity e=world.create();
    auto& asset=world.add<AnimationAssetComponent>(e);
    asset.model=&model;asset.graph=graph;
    world.add<AnimationPlayerComponent>(e);
    world.add<AnimationPoseComponent>(e);
    world.add<AnimationPhaseSeedComponent>(e,AnimationPhaseSeedComponent{.5f});
    MotionWeave::update(world,.01f);
    auto& player=world.get<AnimationPlayerComponent>(e);
    assert(player.instance && player.instance->clipTime("move"));
    assert(std::abs(*player.instance->clipTime("move")-1.01f)<.001f);
    assert(world.get<AnimationPhaseSeedComponent>(e).normalized<0);
}
