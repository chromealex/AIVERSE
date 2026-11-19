// Optional read-only PHYSIS/NEREID ECS composition for native game hosts.
// No core dependency inversion: PHYSIS remains independent of water, NEREID
// remains independent of PHYSIS. Never use this presentation/query view to
// own, simulate or overwrite authoritative NEREID water bodies.
#pragma once
#include "engine/physis/PhysisNatureSampling.h"
#include <aiverselib/water/NereidWaterSystems.h>

namespace aiverse::world {

struct NatureWaterContactView {
    physis::NatureContactComponent nature{};
    lib::nereid::WaterSample water{};
    std::uint32_t sampledSimulationTick = 0;
    bool hasWater = false; // false outside any water footprint
};

// Compose output from two optional native ECS samplers on ONE entity.
// A valid result requires an enabled and freshly sampled nature probe, a
// water probe at the same Fixed position and matching Simulation tick stamps.
// No NEREID WaterBody/WaterContact or PHYSIS climate/receiver gets mutated.
// The host must schedule both samplers after authoritative movement, and
// should set PHYSIS FloodMode::ExternalAuthority for NEREID-owned water.
inline bool readNatureWaterContact(const ::aiverse::World& world,
                                   Entity entity,
                                   NatureWaterContactView& result) {
    const auto* climate = world.resourceConst<physis::State>();
    const auto* natureProbe = world.tryGetConst<physis::NatureProbeComponent>(entity);
    const auto* nature = world.tryGetConst<physis::NatureContactComponent>(entity);
    const auto* waterProbe = world.tryGetConst<lib::nereid::WaterProbeComponent>(entity);
    const auto* water = world.tryGetConst<lib::nereid::WaterContactComponent>(entity);
    if (!climate || !world.alive(entity) || !natureProbe || !natureProbe->enabled ||
        !nature || !nature->valid || !waterProbe || !water ||
        // A pair of old contacts can agree with each other but still
        // belong to an earlier PHYSIS climate tick. Reject it until both
        // sampling systems have completed after the current climate update.
        nature->climateTick != climate->tick ||
        nature->sampledSimulationTick != water->sampledTick ||
        nature->position != natureProbe->position ||
        nature->position != waterProbe->position)
        return false;
    NatureWaterContactView value;
    value.nature = *nature;
    value.water = water->sample;
    value.sampledSimulationTick = water->sampledTick;
    value.hasWater = value.water.found;
    result = value;
    return true;
}

} // namespace aiverse::world
