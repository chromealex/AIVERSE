// Shared authored species recipe used by flat and planetary TerraWeave presentation.
#pragma once
#include "engine/world/WorldAssets.h"
#include "engine/render/TerraWeaveBotanicalForest.h"
namespace aiverse::world {
struct ForestSpeciesRecipe {
    terraweave::TreeProfile profile;
    uint64_t seed=0;
    terraweave::ArborMeshSettings mesh{8,true};
    const terraweave::TreePreset* preset=nullptr;
};
inline ForestSpeciesRecipe parseForestSpeciesRecipe(const Json& def){
    if (!def.isObject())
        throw JsonError("world.terraweaveForest.species: expected object");
    terraweave::TreeProfile profile;
    // "preset": botanical species (TerraWeaveTreePresets.h) as defaults
    // for the profile and the organic mesh recipe.
    const terraweave::TreePreset* preset = nullptr;
    if (def.has("preset")) {
        preset = terraweave::findBotanicalPreset(def.getStr("preset"));
        if (!preset) throw JsonError("world.terraweaveForest.species.preset: unknown botanical preset");
        profile = preset->profile;
    }
    profile.speciesId = static_cast<uint32_t>(worldInteger(
        def, "id", 0, 1, INT32_MAX));
    if (preset) {
        if (def.has("minHeightCm")) profile.minHeightCm = static_cast<int32_t>(worldInteger(def, "minHeightCm", 800, 100, 100000));
        if (def.has("maxHeightCm")) profile.maxHeightCm = static_cast<int32_t>(worldInteger(def, "maxHeightCm", 1500, 100, 100000));
    } else {
    profile.minHeightCm = static_cast<int32_t>(worldInteger(
        def, "minHeightCm", 800, 100, 100000));
    profile.maxHeightCm = static_cast<int32_t>(worldInteger(
        def, "maxHeightCm", 1500, 100, 100000));
    profile.trunkRadiusCm = static_cast<int32_t>(worldInteger(
        def, "trunkRadiusCm", 24, 1, 10000));
    profile.primaryBranches = static_cast<uint32_t>(worldInteger(
        def, "branches", 22, 0, 256));
    profile.leafRadiusCm = static_cast<uint32_t>(worldInteger(
        def, "leafRadiusCm", 95, 0, 100000));
    const std::string shape = def.getStr("shape", "broadleaf");
    if (shape == "broadleaf") profile.shape = terraweave::CrownShape::Broadleaf;
    else if (shape == "conifer") profile.shape = terraweave::CrownShape::Conifer;
    else if (shape == "columnar") profile.shape = terraweave::CrownShape::Columnar;
    else throw JsonError("world.terraweaveForest.species: invalid shape");
    profile.whorlSize=uint32_t(worldInteger(def,"whorlSize",0,0,8));
    if(profile.whorlSize==1 || (profile.whorlSize&&profile.shape!=terraweave::CrownShape::Conifer))throw JsonError("world.terraweaveForest.whorlSize requires conifer and 2..8");
    if(def.has("roundedConiferCrown")&&def["roundedConiferCrown"].type()!=Json::Type::Bool)throw JsonError("world.terraweaveForest.roundedConiferCrown requires boolean");
    profile.roundedConiferCrown=def.getBool("roundedConiferCrown",false);
    if(profile.roundedConiferCrown&&profile.shape!=terraweave::CrownShape::Conifer)throw JsonError("world.terraweaveForest.roundedConiferCrown requires conifer");
    }
    if(!profile.speciesId)throw JsonError("world.terraweaveForest.species.id: required");
    const auto seed=static_cast<uint64_t>(worldInteger(def,"prototypeSeed",profile.speciesId,0,INT32_MAX));
    return {profile,seed,preset?terraweave::botanicalArborMeshSettings(*preset):terraweave::ArborMeshSettings{8,true},preset};
}
} // namespace aiverse::world
