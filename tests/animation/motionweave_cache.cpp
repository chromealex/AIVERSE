#include <aiverselib/motionweave/MotionWeave.h>
#include <cassert>
using namespace aiverse;
using namespace aiverse::motionweave;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    render::Model model;
    model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    auto graph=render::AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));
    World p;
    std::vector<Entity> entities;
    for(size_t i=0;i<128;++i) {
        Entity e=p.create();entities.push_back(e);
        auto& asset=p.add<AnimationAssetComponent>(e);
        asset.model=&model;asset.graph=graph;
        p.add<AnimationPlayerComponent>(e);
        p.add<AnimationPoseComponent>(e);
        auto& lod=p.add<AnimationBudgetComponent>(e);
        lod.visible=true;
    }
    MotionWeaveScheduler scheduler(3);
    const auto first=scheduler.update(p,.016f);
    assert(first.uniqueGraphs==1 && first.eligible==128);
    for(size_t i=0;i<64;++i)p.get<AnimationBudgetComponent>(entities[i]).visible=false;
    const auto second=scheduler.update(p,.016f);
    assert(second.uniqueGraphs==1);
    assert(second.offscreenEntities==64);
    assert(second.estimatedSafeCost<=first.estimatedSafeCost);
    p.get<AnimationPlayerComponent>(entities[0]).restart=true;
    const auto third=scheduler.update(p,.016f);
    assert(third.uniqueGraphs==1);
    return 0;
}
