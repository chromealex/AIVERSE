// Stage 99: generic model source contract (native run deferred).
#include "engine/render/ModelResourceCache.h"
#include "modules/rts/view/RtsModelResourceCache.h"
#include "modules/rts/view/RtsModelPreparationService.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <type_traits>
#include <vector>

using namespace aiverse;
int main() {
    static_assert(std::is_same_v<
        game::view_adapter::RtsModelResourceCache,
        render::ModelResourceCache>);
    static_assert(std::is_same_v<
        game::view_adapter::RtsModelPreparationService,
        render::ModelPreparationService>);
    render::ModelResourceCache owner;
    AIVERSE_TEST_CHECK(owner.generation()==0);
    auto failed=owner.insertIfAbsent("models/missing.glb",nullptr);
    AIVERSE_TEST_CHECK(failed==nullptr && owner.contains("models/missing.glb"));
    AIVERSE_TEST_CHECK(owner.generation()==1);
    auto geometry=std::make_unique<render::Model>();
    geometry->path="models/hero.glb";
    render::Model* hero=owner.insertIfAbsent("models/hero.glb#geometry",
                                             std::move(geometry));
    AIVERSE_TEST_CHECK(hero!=nullptr && owner.generation()==2);
    AIVERSE_TEST_CHECK(owner.find("models/hero.glb#geometry")==hero);
    const auto affected=owner.affectedBySource("models/hero.glb");
    AIVERSE_TEST_CHECK(affected.size()==1 && affected[0].model==hero);
    AIVERSE_TEST_CHECK(owner.affectedBySource("models/missing.glb").size()==1);
    AIVERSE_TEST_CHECK(render::ModelResourceCache::sourceForKey(
        "models/./hero.glb#geometry")=="models/hero.glb");
    const auto names=owner.modelFiles();
    AIVERSE_TEST_CHECK(names.size()==2);
    AIVERSE_TEST_CHECK(owner.pendingRetirements()==0);
}
