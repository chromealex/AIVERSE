#include "engine/animation/EcsAnimationSystems.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Host tests do not link the platform runtime, matching animation_graphs.
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
static void require(bool ok,const char* msg) {
    if (!ok) { std::fprintf(stderr,"animation ECS: %s\n",msg); std::exit(1); }
}
int main() {
    Model m; m.nodes.resize(1); m.nodes[0].name="root"; m.order={0};
    AnimClip clip; clip.name="Move"; clip.duration=1.f;
    AnimChannel c; c.node=0; c.times={0.f,1.f};
    c.values={0.f,0.f,0.f,2.f,0.f,0.f};
    clip.channels.push_back(c); m.clips.push_back(clip);
    const auto graph=AnimationGraph::compile(m,Json::parse(R"({
        "parameters":{"speed":1},
        "nodes":[{"id":"move","kind":"clip","clip":"Move","parameter":"speed"}],
        "output":"move"})"));
    World rts,fps;
    auto create=[&](World& w) {
        Entity e=w.create();
        auto& asset=w.add<AnimationAssetComponent>(e);
        asset.model=&m; asset.graph=graph;
        w.add<AnimationInputsComponent>(e);
        w.add<AnimationPlayerComponent>(e);
        w.add<AnimationPoseComponent>(e);
        w.add<AnimationBudgetComponent>(e);
        w.add<AnimationDiagnosticsComponent>(e);
        return e;
    };
    Entity a=create(rts),b=create(fps);
    stepAnimationPresentation(rts,.25f); stepAnimationPresentation(fps,.5f);
    require(rts.get<AnimationPoseComponent>(a).valid,"RTS pose");
    require(fps.get<AnimationPoseComponent>(b).valid,"FPS pose");
    require(std::abs(rts.get<AnimationPoseComponent>(a).local[0].t.x-.5f)<.001f,"RTS time");
    require(std::abs(fps.get<AnimationPoseComponent>(b).local[0].t.x-1.f)<.001f,"FPS time");
    rts.get<AnimationBudgetComponent>(a).interval=.2f;
    stepAnimationPresentation(rts,.1f);
    require(rts.get<AnimationDiagnosticsComponent>(a).evaluations==1,"throttled");
    stepAnimationPresentation(rts,.1f);
    require(rts.get<AnimationDiagnosticsComponent>(a).evaluations==2,"updated");
    require(std::abs(rts.get<AnimationPoseComponent>(a).local[0].t.x-.9f)<.001f,"LOD clock");
    rts.get<AnimationInputsComponent>(a).overrides.set("speed",0.f);
    stepAnimationPresentation(rts,.2f);
    require(std::abs(rts.get<AnimationPoseComponent>(a).local[0].t.x-.9f)<.001f,"parameter");
    rts.get<AnimationInputsComponent>(a).overrides.values.clear();
    rts.get<AnimationPlayerComponent>(a).restart=true;
    stepAnimationPresentation(rts,.1f);
    require(std::abs(rts.get<AnimationPoseComponent>(a).local[0].t.x-.2f)<.001f,"restart");
    fps.get<AnimationAssetComponent>(b).graph.reset();
    stepAnimationPresentation(fps,.1f);
    require(!fps.get<AnimationPoseComponent>(b).valid,"invalidated");
    require(!fps.get<AnimationDiagnosticsComponent>(b).healthy,"diagnostic");
    std::puts("animation ECS presentation OK");
}
