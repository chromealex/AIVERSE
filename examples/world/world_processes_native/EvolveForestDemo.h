#pragma once
#include "WorldProcessGameplayHost.h"
#include <array>
#include <stdexcept>

namespace aiverse::examples::world_processes_native {
// Resolve these entities from the demo map and real PHYSIS grid before setup.
struct DemoClimateBinding {
    Entity producer,region,weatherCell;
    evolve::ClimateLink settings;
};
// Supply actual TerraWeave anchors from the game's species/terrain owner.
// No synthetic tree IDs, weather cells or second plant registry.
inline void setupEvolveForestDemo(
        World& world,
        const std::array<DemoClimateBinding,3>& climate,
        const std::vector<terraweave::AnchoredPlant>& plants){
    const auto* authority=world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    if(!authority||plants.empty())
        throw std::invalid_argument("EVOLVE demo requires terrain authority and rooted plants");
    World staged=world;
    auto updated=*authority;
    terraweave::RootSupportRegistry roots;
    auto combined=updated.rooted;
    combined.insert(combined.end(),plants.begin(),plants.end());
    roots.setSnapshot(combined);
    if(updated.stateRevision==UINT64_MAX)
        throw std::overflow_error("EVOLVE demo terrain revision overflow");
    updated.rooted=roots.snapshot();
    ++updated.stateRevision;
    for(const auto& link:climate){
        std::string error;
        if(!attachEvolveClimate(staged,link.producer,link.region,
                                link.weatherCell,link.settings,&error))
            throw std::invalid_argument(error);
    }
    staged.setResource<terraweave::TerraWeaveAuthorityResource>(std::move(updated));
    world=std::move(staged);
}
} // namespace aiverse::examples::world_processes_native
