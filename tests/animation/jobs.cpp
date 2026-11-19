#include <aiverselib/animation/AnimationJobScheduler.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::lib::animation;
using namespace aiverse::render;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main() {
    Model m;m.nodes.resize(1);m.nodes[0].name="root";m.order={0};
    AnimClip clip;clip.name="Move";clip.duration=1;
    AnimChannel c;c.node=0;c.times={0,1};c.values={0,0,0,2,0,0};
    clip.channels.push_back(c);m.clips.push_back(clip);
    auto graph=AnimationGraph::compile(m,Json::parse(R"({
        "nodes":[{"id":"c","kind":"clip","clip":"Move"}],"output":"c"})"));
    World a,b;
    auto populate=[&](World& w){
        std::vector<Entity> entities;entities.reserve(96);
        for(int i=0;i<96;++i) {
            auto e=w.create();entities.push_back(e);
            auto& asset=w.add<AnimationAssetComponent>(e);
            asset.model=&m;asset.graph=graph;
            w.add<AnimationPlayerComponent>(e);
            w.add<AnimationPoseComponent>(e);
        }
        return entities;
    };
    auto aa=populate(a),bb=populate(b);
    stepAnimationPresentation(a,.25f);
    const bool used=stepAnimationPresentationParallel(b,.25f,4);
    assert(used);
    for(size_t i=0;i<aa.size();++i) {
        const auto& x=a.get<AnimationPoseComponent>(aa[i]);
        const auto& y=b.get<AnimationPoseComponent>(bb[i]);
        assert(x.valid && y.valid);
        assert(std::abs(x.local[0].t.x-y.local[0].t.x)<1.e-5f);
    }
    assert(canEvaluateAnimationParallel(b));
    return 0;
}
