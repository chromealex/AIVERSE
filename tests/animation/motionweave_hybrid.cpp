// Serial vs hybrid MotionWeave graph parity; callback remains on main thread.
#include "engine/animation/MotionWeave.h"
#include <atomic>
#include "../AlwaysCheck.h"
#include <cmath>
#include <thread>
#include <vector>
using namespace aiverse;
using namespace aiverse::motionweave;
using namespace aiverse::render;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
static std::thread::id owner;
static std::atomic<bool> offThread=false;
static void callback(const Model&,const Json&,const AnimationParameters&,
                     std::span<const float>,Pose&) noexcept {
    if(std::this_thread::get_id()!=owner)offThread.store(true);
}
int main(){
    owner=std::this_thread::get_id();
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimClip clip;clip.name="Walk";clip.duration=1;
    AnimChannel channel;channel.node=0;
    channel.times={0,1};channel.values={0,0,0,2,0,0};
    clip.channels.push_back(channel);model.clips.push_back(clip);
    auto safe=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"walk","kind":"clip","clip":"Walk"}],"output":"walk"})"));
    auto operation=std::make_shared<AnimationOperation>();
    operation->evaluate=&callback;
    auto unsafe=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"},
                 {"id":"custom","kind":"native","input":"bind","operation":"test"}],
        "output":"custom"})"),{{"test",operation}});
    World a,b;
    auto populate=[&](World& world){
        std::vector<Entity> entities;
        for(int i=0;i<100;++i){
            Entity e=world.create();
            auto& asset=world.add<AnimationAssetComponent>(e);
            asset.model=&model;
            asset.graph=i%19==0?unsafe:safe;
            world.add<AnimationPlayerComponent>(e);
            world.add<AnimationPoseComponent>(e);
            entities.push_back(e);
        }
        return entities;
    };
    auto aa=populate(a),bb=populate(b);
    MotionWeaveScheduler scheduler(3);
    for(int frame=0;frame<5;++frame){
        MotionWeave::update(a,.05f);
        auto stats=MotionWeave::update(b,.05f,scheduler);
        AIVERSE_TEST_CHECK(stats.eligible==100);
        AIVERSE_TEST_CHECK(stats.serialEntities==6);
        AIVERSE_TEST_CHECK(stats.parallelEntities==94);
        AIVERSE_TEST_CHECK(stats.usedWorkers==(scheduler.workerCount()>0));
        AIVERSE_TEST_CHECK(!offThread.load());
        for(size_t i=0;i<aa.size();++i) {
            const auto& s=a.get<AnimationPoseComponent>(aa[i]);
            const auto& p=b.get<AnimationPoseComponent>(bb[i]);
            AIVERSE_TEST_CHECK(s.valid && p.valid && s.local.size()==p.local.size());
            AIVERSE_TEST_CHECK(std::abs(s.local[0].t.x-p.local[0].t.x)<1.e-4f);
        }
    }
}
