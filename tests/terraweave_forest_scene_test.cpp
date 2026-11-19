#include "engine/world/TerraWeaveForestScene.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::terraweave;

namespace {
std::vector<FoliageInstance> flatten(const TerraWeaveForestScene& scene) {
    std::vector<FoliageInstance> result;
    for (const auto& batch : scene.visibleBatches())
        result.insert(result.end(), batch.instances.begin(), batch.instances.end());
    return result;
}
}
int main() {
    ForestSceneSettings settings;
    settings.chunkSizeCm = 2400;
    settings.radiusChunks = 0;
    settings.maxResidentChunks = 1;
    settings.scatter.spacingCm = 450;
    settings.scatter.densityPermille = 1000;
    settings.terrain.baseHeightCm = 0;
    settings.terrain.reliefCm = 0;

    TerraWeaveForestScene scene(settings);
    TreeProfile profile;
    profile.speciesId = 77;
    profile.primaryBranches = 5;
    FoliageSpecies placement;
    placement.id = 77;
    placement.biomeMask = kLandBiomes | biomeBit(Biome::Ocean);
    placement.maxSlopePermille = 100000;
    scene.addSpecies(profile, placement, 7);

    scene.setHeightSource([](int64_t x, int64_t z) -> int32_t {
        (void)x; (void)z; return 1234;
    });
    scene.setPlacementFilter([](const FoliageInstance& instance) {
        return instance.xCm >= 600;
    });
    scene.streamAt({600, 600});
    const auto initial = flatten(scene);
    assert(!initial.empty());
    const auto revision = scene.stats().revision;
    for (const auto& v : initial) {
        assert(v.heightCm == 1234);
        assert(v.xCm >= 600);
    }
    scene.setHeightSource([](int64_t, int64_t) -> int32_t {
        return 4321;
    });
    assert(scene.stats().revision > revision);
    scene.streamAt({600, 600});
    const auto changed = flatten(scene);
    assert(changed.size() == initial.size());
    for (size_t i = 0; i < initial.size(); ++i) {
        assert(changed[i].key == initial[i].key);
        assert(changed[i].xCm == initial[i].xCm);
        assert(changed[i].zCm == initial[i].zCm);
        assert(changed[i].heightCm == 4321);
    }

    scene.setPlacementFilter([](const FoliageInstance&) { return false; });
    scene.streamAt({600, 600});
    assert(scene.stats().instances == 0);
    assert(scene.visibleBatches().empty());

    scene.setPlacementFilter({});
    scene.streamAt({600, 600});
    assert(scene.stats().instances > 0);

    scene.setHeightSource([](int64_t, int64_t) -> int32_t {
        return INT32_MAX;
    });
    bool threw = false;
    try { scene.streamAt({600, 600}); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}
