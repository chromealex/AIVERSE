#include "engine/animation/MotionWeave.h"
#include "../AlwaysCheck.h"
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::motionweave;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    Model model;
    model.nodes.resize(3);model.order={0,1,2};
    model.nodes[0].name="root";model.nodes[0].children={1};
    model.nodes[1].name="knee";model.nodes[1].parent=0;
    model.nodes[1].children={2};model.nodes[1].t={0,1,0};
    model.nodes[2].name="foot";model.nodes[2].parent=1;
    model.nodes[2].t={0,1,0};
    auto graph=AnimationGraph::compile(model,Json::parse(R"({
        "parameters":{"target":[1,1,0]},
        "nodes":[{"id":"bind","kind":"bind"},
          {"id":"ik","kind":"rig","input":"bind","units":[{
            "op":"fullBodyIK","bone":"root",
            "effectors":[{"bone":"foot","target":"target"}]
          }]}],
        "output":"ik"})"));
    AIVERSE_TEST_CHECK(workerSafeGraph(*graph));
    World serial,parallel;
    auto create=[&](World& w) {
        std::vector<Entity> es;
        for(int i=0;i<80;++i) {
            Entity e=w.create();
            auto& asset=w.add<AnimationAssetComponent>(e);
            asset.model=&model;asset.graph=graph;
            w.add<AnimationPlayerComponent>(e);
            w.add<AnimationPoseComponent>(e);
            es.push_back(e);
        }
        return es;
    };
    auto a=create(serial),b=create(parallel);
    MotionWeaveScheduler scheduler(3);
    MotionWeave::update(serial,.033f);
    const auto stats=MotionWeave::update(parallel,.033f,scheduler);
    AIVERSE_TEST_CHECK(stats.serialEntities==0 && stats.parallelEntities==80);
    AIVERSE_TEST_CHECK(stats.usedWorkers==(scheduler.workerCount()>0));
    for(int i=0;i<80;++i) {
        auto& x=serial.get<AnimationPoseComponent>(a[size_t(i)]);
        auto& y=parallel.get<AnimationPoseComponent>(b[size_t(i)]);
        AIVERSE_TEST_CHECK(x.valid && y.valid);
        AIVERSE_TEST_CHECK(std::abs(x.local[0].t.x-y.local[0].t.x)<1.e-4f);
    }
}
