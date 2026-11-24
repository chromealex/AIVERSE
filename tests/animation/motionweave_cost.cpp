#include "engine/animation/AnimationWorkPolicy.h"
#include "engine/animation/MotionWeave.h"
#include "../AlwaysCheck.h"
#include <cmath>
using namespace aiverse;
using namespace aiverse::motionweave;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main(){
    std::vector<uint32_t> costs={1,1,1,120,1,1,1,1,180,1,1,1};
    auto chunks=partitionAnimationWork(costs,6,1);
    AIVERSE_TEST_CHECK(!chunks.empty() && chunks.size()<=6);
    size_t cursor=0;uint64_t summed=0;
    for(const auto& c:chunks){
        AIVERSE_TEST_CHECK(c.begin==cursor && c.end>c.begin);
        cursor=c.end;summed+=c.estimatedCost;
    }
    AIVERSE_TEST_CHECK(cursor==costs.size() && summed==310);
    AIVERSE_TEST_CHECK(scaledAnimationCost(100,2.f)==200);
    AIVERSE_TEST_CHECK(scaledAnimationCost(100,NAN)==100);
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
    AIVERSE_TEST_CHECK(stats.eligible==96);
    AIVERSE_TEST_CHECK(stats.serialEntities==1);
    AIVERSE_TEST_CHECK(stats.parallelEntities==95);
    AIVERSE_TEST_CHECK(stats.estimatedSafeCost>0);
    AIVERSE_TEST_CHECK(!stats.usedWorkers); // bind-only crowd stays below default cost gate
    auto options=scheduler.options();
    options.minimumParallelCost=1;
    scheduler.setOptions(options);
    const auto parallel=scheduler.update(w,.01f);
    AIVERSE_TEST_CHECK(parallel.usedWorkers==(scheduler.workerCount()>0));
    AIVERSE_TEST_CHECK(parallel.parallelEntities==95 && parallel.serialEntities==1);
    AIVERSE_TEST_CHECK(stats.workerPhaseMs>=0 && stats.serialPhaseMs>=0);
}
