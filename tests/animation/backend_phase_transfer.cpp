#include <aiverselib/animation/AnimationPhaseTransfer.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimClip walk;walk.name="Walk";walk.duration=2.f;model.clips.push_back(walk);
    auto graph=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"move","kind":"clip","clip":"Walk"}],"output":"move"})"));
    AnimationGraphInstance inst;inst.reset(graph);
    assert(inst.seekClipNode("move",1.4f));
    auto phase=captureGraphClipPhase(inst,model,true);
    assert(phase && std::abs(*phase-.7f)<1.e-5f);
    Animator crowd;crowd.setModel(&model);crowd.play(0,0,{});
    assert(crowd.seek(0,*phase*model.clips[0].duration));
    auto baked=captureBakedClipPhase(crowd,model);
    assert(baked && std::abs(*baked-*phase)<1.e-5f);
    assert(!captureGraphClipPhase(inst,model,false) || *phase>=0.f);
}
