// Shared render recipe built on the CPU-only species schema.
#pragma once
#include "engine/world/TerraWeaveForestSpeciesProfile.h"
#include "engine/render/TerraWeaveBotanicalForest.h"
namespace aiverse::world {
struct ForestSpeciesRecipe {
    terraweave::TreeProfile profile;
    uint64_t seed=0;
    terraweave::ArborMeshSettings mesh{8,true};
    const terraweave::TreePreset* preset=nullptr;
};
inline ForestSpeciesRecipe parseForestSpeciesRecipe(const Json& def){
    const auto source=parseForestSpeciesProfile(def);
    return {source.profile,source.seed,source.preset?terraweave::botanicalArborMeshSettings(*source.preset):terraweave::ArborMeshSettings{8,true},source.preset};
}
} // namespace aiverse::world
