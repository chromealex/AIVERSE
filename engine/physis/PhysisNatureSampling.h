// PHYSIS Stage 78 — optional genre-neutral ECS environmental contacts.
// The authoritative inputs are PHYSIS climate/wind/gravity/storm ECS fields.
// Contact values are derived, tick-stamped outputs; no water or motion solver.
// Consumers (AI, physics, vegetation, VFX, water presentation) read these
// components after the PHYSIS update barrier. No renderer/NEREID dependency.
#pragma once
#include "PhysisPresentation.h"
#include "../core/Archive.h"
#include <cstdint>

namespace aiverse::physis {

// A host owns motion and updates position before the PHYSIS nature-contact
// system. This input is separate from NEREID WaterProbeComponent by design:
// the two optional systems can coexist on the SAME ECS entity.
struct NatureProbeComponent {
    Vec3 position{}; // world metres, Fixed Q47.16
    bool enabled = true;
};

// Snapshot of *derived* environment for one entity and one authoritative tick.
// This contains no graphics handles, mutable water state, runtime pointers or
// nondeterministic floating-point values. The explicit optional persistence
// schema supports replay/rollback parity when projects enable it.
struct NatureContactComponent {
    Vec3 position{};
    Vec3 windVelocity{};          // m/s, includes local gusts and storms
    Vec3 gravityAcceleration{};   // m/s^2, not force in Newtons
    Fixed temperature{};          // Celsius
    Fixed humidity{};             // normalized
    Fixed pressureHpa{};
    Fixed rain{};                 // normalized; NOT water metres per tick
    Fixed snow{};                 // normalized
    Fixed wetness{};
    Fixed snowpack{};             // metres of snow water equivalent
    Fixed ice{};
    Fixed runoffPotential{};      // normalized, not hydrologic volume
    Fixed fire{};                 // normalized
    Fixed quake{};                // normalized
    std::uint64_t climateTick = 0;
    std::uint32_t sampledSimulationTick = 0; // completed when Simulation advances
    bool hasAtmosphere = false;
    bool hasHazards = false;
    bool valid = false;
};

inline void io(Archive& archive, NatureProbeComponent& component) {
    archive(component.position, component.enabled);
}
inline void io(Archive& archive, NatureContactComponent& component) {
    archive(component.position, component.windVelocity,
            component.gravityAcceleration, component.temperature,
            component.humidity, component.pressureHpa, component.rain,
            component.snow, component.wetness, component.snowpack,
            component.ice, component.runoffPotential, component.fire,
            component.quake, component.climateTick,
            component.sampledSimulationTick, component.hasAtmosphere,
            component.hasHazards, component.valid);
}

// Read: NatureProbe + PHYSIS inputs. Write: NatureContact only.
// No structural ECS mutations, no local RNG, no advancement of PHYSIS climate.
// Missing/disabled probes are explicitly invalidated, never left with stale
// sampled values from an earlier authoritative tick.
inline void advanceNatureContacts(World& world, std::uint32_t simulationTick) {
    auto* contacts = world.prepareExistingWrite<NatureContactComponent>();
    if (!contacts) return;
    const World& read = world;
    for (std::size_t i = 0; i < contacts->size(); ++i) {
        const Entity entity = contacts->entityAt(i);
        if (!read.alive(entity)) continue;
        auto& output = contacts->dataAt(i);
        output = NatureContactComponent{};
        output.sampledSimulationTick = simulationTick;
        const auto* probe = read.tryGetConst<NatureProbeComponent>(entity);
        if (!probe || !probe->enabled) continue;

        NatureFieldSample sampled;
        if (!sampleNatureField(read, probe->position, sampled)) continue;
        output.position = sampled.position;
        output.windVelocity = sampled.windVelocity;
        output.gravityAcceleration = sampled.gravityAcceleration;
        output.pressureHpa = sampled.pressureHpa;
        output.climateTick = sampled.tick;
        output.hasAtmosphere = sampled.hasAtmosphere;
        output.hasHazards = sampled.hasHazards;
        if (sampled.hasAtmosphere) {
            output.temperature = sampled.environment.temperature;
            output.humidity = sampled.environment.humidity;
            output.rain = sampled.environment.rain;
            output.snow = sampled.environment.snow;
            output.wetness = sampled.environment.wetness;
            output.snowpack = sampled.environment.snowpack;
            output.ice = sampled.environment.ice;
            output.runoffPotential = sampled.environment.runoffPotential;
        }
        if (sampled.hasHazards) {
            output.fire = sampled.hazards.fire;
            output.quake = sampled.hazards.quake;
        }
        output.valid = true;
    }
}

} // namespace aiverse::physis
