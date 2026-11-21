#include "engine/world/TerraWeaveForestScene.h"
#include "engine/world/TerraWeaveForestManifest.h"
#include "engine/render/WindRigModel.h"
#include <algorithm>
#include <string>
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

// WorldScene manifest: botanical presets + textures + GPU wind -> merged,
// textured near trees and an impostor baked from them.
void manifestBotanical() {
    using namespace aiverse::terraweave;
    using aiverse::render::AlphaMode;
    using aiverse::render::wind::findWindRig;
    // 2x2 BGRA TGA (decodeImage detects it by content, not by name).
    std::string tga(18, '\0');
    tga[2] = 2; tga[12] = 2; tga[14] = 2; tga[16] = 32; tga[17] = 8;
    for (int i = 0; i < 4; ++i) tga += std::string("\x20\x80\x30\xff", 4);
    std::vector<std::string> reads;
    aiverse::world::AssetReader reader = [&](const std::string& path, std::string& bytes) {
        reads.push_back(path);
        if (path.rfind("tex/", 0) != 0) return false;
        bytes = tga;
        return true;
    };
    const auto manifest = aiverse::Json::parse(R"({"terraweaveForest":{"hierarchicalWind":true,
        "botanicalTextures":"tex","species":[{"id":5,"preset":"oak"},{"id":6,"shape":"conifer"}]}})");
    auto scene = aiverse::world::forestSceneFromManifest(manifest, reader);
    assert(scene != nullptr);
    const auto* oak = scene->adapter().prototype(5);
    const auto* plain = scene->adapter().prototype(6);
    assert(oak && plain);
    assert(oak->nodes.size() == 1 && plain->nodes.size() == 1);   // GPU wind -> merged
    // Wind-rig textures live in model images too: compare, do not expect 0.
    assert(oak->images.size() >= plain->images.size() + 2);
    assert(plain->materials[0].baseTex < 0 || plain->materials[0].baseTex >= int(plain->images.size()) ||
           plain->images[size_t(plain->materials[0].baseTex)].name.rfind("tex/", 0) != 0);
    assert(oak->materials[1].alpha == AlphaMode::Mask && oak->materials[1].baseTex >= 0);
    assert(std::any_of(reads.begin(), reads.end(), [](const std::string& r) { return r == "tex/oak_leaves_basecolor.png"; }));
    assert(findWindRig(*oak).has_value());
    // Impostor baked from the textured near tree: some opaque texels carry
    // the leaf texture colour (green > red).
    const auto* far = scene->adapter().billboardPrototype(5);
    assert(far && !far->images.empty());
    size_t green = 0;
    for (size_t i = 0; far && i + 3 < far->images[0].rgba.size(); i += 4)
        if (far->images[0].rgba[i + 3] && far->images[0].rgba[i + 1] > far->images[0].rgba[i]) ++green;
    assert(green > 100);
    bool unknown = false;
    try {
        (void)aiverse::world::forestSceneFromManifest(aiverse::Json::parse(
            R"({"terraweaveForest":{"species":[{"id":1,"preset":"baobab"}]}})"));
    } catch (const std::exception&) { unknown = true; }
    assert(unknown);
}
}
int main() {
    manifestBotanical();
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
