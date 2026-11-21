// Standalone CPU-only MotionWeave performance fixture.
// Build with AIVERSE_BUILD_TOOLS=ON; results depend on real hardware.
//   aiverse_motionweave_bench --actors 1000 --frames 120 [--ik]
#include <aiverselib/motionweave/MotionWeave.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::motionweave;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
static size_t number(const char* value) {
    size_t result=0;
    std::string_view str(value);
    const auto parsed=std::from_chars(str.data(),str.data()+str.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=str.data()+str.size())
        throw std::invalid_argument("invalid unsigned count");
    return result;
}
static std::vector<Entity> makeActors(World& w,const Model& model,
        std::shared_ptr<const AnimationGraph> graph,size_t count) {
    std::vector<Entity> ids;ids.reserve(count);
    for(size_t i=0;i<count;++i) {
        Entity e=w.create();ids.push_back(e);
        auto& asset=w.add<AnimationAssetComponent>(e);
        asset.model=&model;asset.graph=graph;
        w.add<AnimationPlayerComponent>(e);
        w.add<AnimationPoseComponent>(e);
        auto& budget=w.add<AnimationBudgetComponent>(e);
        budget.interval=i%7==0?1.f/30.f:0.f;
    }
    return ids;
}
int main(int argc,char** argv) {
    try {
        size_t actors=1000,frames=120;bool ik=false;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--actors"&&i+1<argc)actors=number(argv[++i]);
            else if(option=="--frames"&&i+1<argc)frames=number(argv[++i]);
            else if(option=="--ik")ik=true;
            else throw std::invalid_argument(
                "usage: aiverse_motionweave_bench --actors 1000 --frames 120 [--ik]");
        }
        if(actors<1||actors>10000||frames<1||frames>1000)
            throw std::invalid_argument("actors 1..10000 and frames 1..1000");
        Model model;model.nodes.resize(3);model.order={0,1,2};
        model.nodes[0].name="root";model.nodes[0].children={1};
        model.nodes[1].name="knee";model.nodes[1].parent=0;
        model.nodes[1].children={2};model.nodes[1].t={0,1,0};
        model.nodes[2].name="foot";model.nodes[2].parent=1;
        model.nodes[2].t={0,1,0};
        AnimClip clip;clip.name="Move";clip.duration=1;
        AnimChannel c;c.node=0;c.times={0,1};c.values={0,0,0,1,0,0};
        clip.channels.push_back(c);model.clips.push_back(clip);
        const Json graphDef=ik?Json::parse(R"({
            "parameters":{"target":[0.5,1.7,0]},
            "nodes":[{"id":"move","kind":"clip","clip":"Move"},
              {"id":"ik","kind":"rig","input":"move","units":[{
                "op":"fullBodyIK","bone":"root",
                "effectors":[{"bone":"foot","target":"target"}]
              }]}],
            "output":"ik"
        })"):Json::parse(R"({
            "nodes":[{"id":"move","kind":"clip","clip":"Move"}],
            "output":"move"
        })");
        auto graph=AnimationGraph::compile(model,graphDef);
        World sequential,concurrent;
        auto seqIds=makeActors(sequential,model,graph,actors);
        auto parIds=makeActors(concurrent,model,graph,actors);
        MotionWeaveScheduler scheduler;
        for(size_t n=0;n<5;++n) {
            MotionWeave::update(sequential,1.f/60.f);
            MotionWeave::update(concurrent,1.f/60.f,scheduler);
        }
        auto start=std::chrono::steady_clock::now();
        for(size_t n=0;n<frames;++n)
            MotionWeave::update(sequential,1.f/60.f);
        const auto mid=std::chrono::steady_clock::now();
        MotionWeaveScheduleStats stats;
        for(size_t n=0;n<frames;++n)
            stats=MotionWeave::update(concurrent,1.f/60.f,scheduler);
        const auto end=std::chrono::steady_clock::now();
        const auto elapsed=[](auto from,auto until,size_t count) {
            return std::chrono::duration<double,std::milli>(until-from).count()
                /double(count);
        };
        const double serialMs=elapsed(start,mid,frames);
        const double parallelMs=elapsed(mid,end,frames);
        const auto& a=sequential.get<AnimationPoseComponent>(seqIds[0]);
        const auto& b=concurrent.get<AnimationPoseComponent>(parIds[0]);
        if(!a.valid || !b.valid || a.local.size()!=b.local.size())
            throw std::runtime_error("serial and parallel poses differ in validity");
        for(size_t n=0;n<a.local.size();++n) {
            if(std::abs(a.local[n].t.x-b.local[n].t.x)>1.e-4f)
                throw std::runtime_error("serial and parallel poses differ");
        }
        std::printf(
            "MotionWeave actors=%zu frames=%zu ik=%d workers=%zu "
            "serial=%.4fms parallel=%.4fms speedup=%.3fx "
            "safe=%zu callbacks=%zu batches=%zu\n",
            actors,frames,int(ik),stats.workerThreads,
            serialMs,parallelMs,parallelMs>0?serialMs/parallelMs:0,
            stats.parallelEntities,stats.serialEntities,stats.batches);
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"MotionWeave benchmark: %s\n",e.what());
        return 1;
    }
}
