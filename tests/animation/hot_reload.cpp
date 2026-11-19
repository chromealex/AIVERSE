#include <aiverselib/animation/AnimationHotReload.h>
#include <cassert>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main() {
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimationHotReloadRegistry registry;
    const auto good=Json::parse(R"({"nodes":[{"id":"a","kind":"bind"}],"output":"a"})");
    assert(registry.compile("hero",model,good,1));
    auto original=registry.lookup("hero")->graph;
    assert(!registry.compile("hero",model,good,1)); // same revision
    assert(!registry.compile("hero",model,Json::parse(R"({"broken":1})"),2));
    assert(registry.lookup("hero")->graph==original);
    assert(!registry.lookup("hero")->error.empty());
    World p;Entity e=p.create();
    auto& asset=p.add<AnimationAssetComponent>(e);
    asset.model=&model;
    p.add<AnimationGraphKeyComponent>(e,AnimationGraphKeyComponent{"hero"});
    assert(registry.apply(p)==1);
    p.add<AnimationPlayerComponent>(e);
    p.add<AnimationPoseComponent>(e);
    stepAnimationPresentation(p,.1f);
    assert(p.get<AnimationPoseComponent>(e).valid);
    assert(registry.compile("hero",model,good,3));
    p.get<AnimationPlayerComponent>(e).restart=false;
    assert(registry.apply(p)==1);
    assert(p.get<AnimationAssetComponent>(e).graph!=original);
    assert(p.get<AnimationPlayerComponent>(e).restart);
    // Retiring a Model invalidates its last-good graph cache before GPU
    // resources are destroyed; reusing its pointer is not a valid cache hit.
    assert(registry.forgetModel(&model)==1);
    assert(registry.lookup("hero")==nullptr);
    return 0;
}
