#include <aiverselib/animation/AnimationWorkPolicy.h>
#include <aiverselib/motionweave/MotionWeave.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::motionweave;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main(){
    std::vector<uint32_t> costs={1,1,1,120,1,1,1,1,180,1,1,1};
    auto chunks=partitionAnimationWork(costs,6,1);
    assert(!chunks.empty() && chunks.size()<=6);
    size_t cursor=0;uint64_t summed=0;
    for(const auto& c:chunks){
        assert(c.begin==cursor && c.end>c.begin);
        cursor=c.end;summed+=c.estimatedCost;
    }
    assert(cursor==costs.size() && summed==310);
    assert(scaledAnimationCost(100,2.f)==200);
    assert(scaledAnimationCost(100,NAN)==100);
    render::Model model;model.nodes.resize(1);model.nodes[0].name="root";
    model.order={0};
    auto graph=render::AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"base","kind":"bind"}],"output":"base"})"));
    World w;
    for(int i=0;i<96;++i) {
        Entity e=w.create();
        auto& asset=w.add<AnimationAssetComponent>(e);
        asset.model=&model;asset.graph=graph;
        w.add<AnimationPlayerComponent>(e);
        w.add<AnimationPoseComponent>(e);
        if(i==1){
            auto& hint=w.add<AnimationWorkHintComponent>(e);
            hint.mainThread=true;
        }
    }
    MotionWeaveScheduler scheduler(3);
    const auto stats=scheduler.update(w,.01f);
    assert(stats.eligible==96);
    assert(stats.serialEntities==1);
    assert(stats.parallelEntities==95);
    assert(stats.estimatedSafeCost>0);
    assert(stats.usedWorkers==(scheduler.workerCount()>0));
    assert(stats.workerPhaseMs>=0 && stats.serialPhaseMs>=0);
}
