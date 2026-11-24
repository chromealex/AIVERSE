// Shared CPU-only authored ARBOR profile and seed. No render dependency.
#pragma once
#include "engine/data/Json.h"
#include "engine/world/TerraWeaveTreePresets.h"
namespace aiverse::world {
struct ForestSpeciesProfile {
    terraweave::TreeProfile profile;
    uint64_t seed=0;
    const terraweave::TreePreset* preset=nullptr;
};
inline int64_t forestProfileInteger(const Json& j,const char* key,int64_t fallback,int64_t low,int64_t high){
    if(!j.has(key))return fallback;
    const auto& v=j[key];
    if(!v.isNumber()||v.fx().raw%Fixed::kOne||v.integer()<low||v.integer()>high)
        throw JsonError(std::string("world.terraweaveForest.")+key+": invalid integer");
    return v.integer();
}
inline ForestSpeciesProfile parseForestSpeciesProfile(const Json& def){
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
    profile.speciesId = static_cast<uint32_t>(forestProfileInteger(
        def, "id", 0, 1, INT32_MAX));
    if (preset) {
        if (def.has("minHeightCm")) profile.minHeightCm = static_cast<int32_t>(forestProfileInteger(def, "minHeightCm", 800, 100, 100000));
        if (def.has("maxHeightCm")) profile.maxHeightCm = static_cast<int32_t>(forestProfileInteger(def, "maxHeightCm", 1500, 100, 100000));
    } else {
    profile.minHeightCm = static_cast<int32_t>(forestProfileInteger(
        def, "minHeightCm", 800, 100, 100000));
    profile.maxHeightCm = static_cast<int32_t>(forestProfileInteger(
        def, "maxHeightCm", 1500, 100, 100000));
    profile.trunkRadiusCm = static_cast<int32_t>(forestProfileInteger(
        def, "trunkRadiusCm", 24, 1, 10000));
    profile.primaryBranches = static_cast<uint32_t>(forestProfileInteger(
        def, "branches", 22, 0, 256));
    profile.leafRadiusCm = static_cast<uint32_t>(forestProfileInteger(
        def, "leafRadiusCm", 95, 0, 100000));
    const std::string shape = def.getStr("shape", "broadleaf");
    if (shape == "broadleaf") profile.shape = terraweave::CrownShape::Broadleaf;
    else if (shape == "conifer") profile.shape = terraweave::CrownShape::Conifer;
    else if (shape == "columnar") profile.shape = terraweave::CrownShape::Columnar;
    else throw JsonError("world.terraweaveForest.species: invalid shape");
    profile.whorlSize=uint32_t(forestProfileInteger(def,"whorlSize",0,0,8));
    if(profile.whorlSize==1 || (profile.whorlSize&&profile.shape!=terraweave::CrownShape::Conifer))throw JsonError("world.terraweaveForest.whorlSize requires conifer and 2..8");
    if(def.has("roundedConiferCrown")&&def["roundedConiferCrown"].type()!=Json::Type::Bool)throw JsonError("world.terraweaveForest.roundedConiferCrown requires boolean");
    profile.roundedConiferCrown=def.getBool("roundedConiferCrown",false);
    if(profile.roundedConiferCrown&&profile.shape!=terraweave::CrownShape::Conifer)throw JsonError("world.terraweaveForest.roundedConiferCrown requires conifer");
    }
    if(!profile.speciesId)throw JsonError("world.terraweaveForest.species.id: required");
    const auto seed=static_cast<uint64_t>(forestProfileInteger(def,"prototypeSeed",profile.speciesId,0,INT32_MAX));
    return {profile,seed,preset};
}
} // namespace aiverse::world
