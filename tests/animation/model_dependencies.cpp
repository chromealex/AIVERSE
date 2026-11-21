// Stage 97 source-level regression fixture (native execution deferred).
#include "modules/rts/view/RtsModelSourceDependencies.h"
#include "../AlwaysCheck.h"
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
    for(const char* path:{
        "models/hero.glb","models/hero-variant.glb",
        "models/hero-lod.glb","models/hero-damaged.glb",
        "models/sword.glb","models/coarse.glb","models/detail.glb"
    }) {
        const auto* e=index.lookup(path);
        AIVERSE_TEST_CHECK(e && e->has(ModelSourceKind::Model));
        AIVERSE_TEST_CHECK(e->types.count(0)==1);
    }
    AIVERSE_TEST_CHECK(index.lookup("animation/hero.json")->has(
        ModelSourceKind::AnimationGraph));
    AIVERSE_TEST_CHECK(index.lookup("materials.json")->has(
        ModelSourceKind::MaterialLibrary));
    AIVERSE_TEST_CHECK(index.lookup("textures/steel.png")->has(ModelSourceKind::Texture));
    AIVERSE_TEST_CHECK(index.lookup("textures/camo.png")->has(ModelSourceKind::Texture));
    AIVERSE_TEST_CHECK(index.lookup("textures/bark-normal.png")->types.count(1)==1);
    AIVERSE_TEST_CHECK(index.lookup("models/missing.glb")==nullptr);
    const auto materialImpact=index.resolve({"materials.json"});
    AIVERSE_TEST_CHECK(materialImpact.requiresWholeView());
    const auto graphImpact=index.resolve({"animation/hero.json"});
    AIVERSE_TEST_CHECK(graphImpact.requiresWholeView());
    const auto textureImpact=index.resolve({"textures/steel.png"});
    AIVERSE_TEST_CHECK(textureImpact.texture && !textureImpact.requiresWholeView());
    AIVERSE_TEST_CHECK(textureImpact.models.count("models/sword.glb")==1);
    AIVERSE_TEST_CHECK(textureImpact.models.count("models/tree.glb")==0);
    index.addResidentTexture("models/hero.glb","textures/imported.png");
    const auto imported=index.resolve({"textures/imported.png"});
    AIVERSE_TEST_CHECK(imported.types.count(0)==1 && imported.types.count(1)==0);
    index.addResidentModel("models/missing.glb"); // failed/null cache key
    const auto missing=index.resolve({"models/missing.glb"});
    AIVERSE_TEST_CHECK(missing.model && missing.models.count("models/missing.glb")==1);
    return 0;
}
