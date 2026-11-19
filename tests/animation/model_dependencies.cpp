// Stage 97 source-level regression fixture (native execution deferred).
#include "modules/rts/view/RtsModelSourceDependencies.h"
#include <cassert>
#include <set>
#include <string>
#include <vector>
using namespace aiverse;
using namespace aiverse::game::view_adapter;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
struct Type { Json view; };
int main() {
    std::vector<Type> types;
    types.push_back({Json::parse(R"({
        "model":["models/hero.glb",
                 {"file":"models/hero-variant.glb",
                  "materials":{"*":{"texture":"textures/camo.png"}}}],
        "lod":[{"model":"models/hero-lod.glb","distance":50}],
        "stages":[{"build":0.5,"model":"models/hero-damaged.glb"}],
        "attach":[{"model":"models/sword.glb"}],
        "streaming":{"coarseModel":"models/coarse.glb",
                     "detailModel":"models/detail.glb"},
        "animationGraph":"animation/hero.json",
        "materials":{"Hull":{"use":"steel"}}
    })")});
    types.push_back({Json::parse(R"({
        "model":"models/tree.glb",
        "materials":{"*":{"normalMap":"textures/bark-normal.png"}}
    })")});
    auto materialLibrary=Json::parse(R"({
        "steel":{"texture":"textures/steel.png",
                 "normalMap":"textures/steel-normal.png"}
    })");
    ModelSourceDependencyIndex index;
    index.rebuild(types,materialLibrary);
    for(const std::string& path:{
        "models/hero.glb","models/hero-variant.glb",
        "models/hero-lod.glb","models/hero-damaged.glb",
        "models/sword.glb","models/coarse.glb","models/detail.glb"
    }) {
        const auto* e=index.lookup(path);
        assert(e && e->has(ModelSourceKind::Model));
        assert(e->types.count(0)==1);
    }
    assert(index.lookup("animation/hero.json")->has(
        ModelSourceKind::AnimationGraph));
    assert(index.lookup("materials.json")->has(
        ModelSourceKind::MaterialLibrary));
    assert(index.lookup("textures/steel.png")->has(ModelSourceKind::Texture));
    assert(index.lookup("textures/camo.png")->has(ModelSourceKind::Texture));
    assert(index.lookup("textures/bark-normal.png")->types.count(1)==1);
    assert(index.lookup("models/missing.glb")==nullptr);
    const auto materialImpact=index.resolve({"materials.json"});
    assert(materialImpact.requiresWholeView());
    const auto graphImpact=index.resolve({"animation/hero.json"});
    assert(graphImpact.requiresWholeView());
    const auto textureImpact=index.resolve({"textures/steel.png"});
    assert(textureImpact.texture && !textureImpact.requiresWholeView());
    assert(textureImpact.models.count("models/sword.glb")==1);
    assert(textureImpact.models.count("models/tree.glb")==0);
    index.addResidentTexture("models/hero.glb","textures/imported.png");
    const auto imported=index.resolve({"textures/imported.png"});
    assert(imported.types.count(0)==1 && imported.types.count(1)==0);
    index.addResidentModel("models/missing.glb"); // failed/null cache key
    const auto missing=index.resolve({"models/missing.glb"});
    assert(missing.model && missing.models.count("models/missing.glb")==1);
    return 0;
}
