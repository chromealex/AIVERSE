// PHYSIS Stage 13: one read-only, genre-neutral 3D environmental sampling API.
// Future Terra/NEREID/VFX/Physics adapters may consume copied Fixed values;
// no dependency on their engines or on a render/graphics backend.
#pragma once
#include "PhysisForces.h"

namespace aiverse::physis {

struct NatureFieldSample {
    Vec3 position{};               // caller's world coordinates (Fixed metres)
    std::uint64_t tick = 0;
    bool hasAtmosphere = false;    // false when outside PHYSIS climate grid
    EnvironmentalResponse environment{};
    Fixed pressureHpa{};          // sampled PHYSIS atmospheric pressure if present
    Vec3 windVelocity{};          // all 3 axes, includes local zones and storm vortex
    Vec3 gravityAcceleration{};   // environment acceleration (NOT a force in N)
    StormInfluence storms{};      // supplemental storm wind/rain/pressure contributions
    bool hasHazards = false;
    HazardCell hazards{};         // fire/quake/standalone shallow flood only
};

// Successful when a PHYSIS World is initialized, even outside its grid:
// gravity/wind/global storm fields may still be defined there. The
// hasAtmosphere/hasHazards flags describe which optional grid data is present.
// No ECS writes, no hidden mutable caches and no reflection/renderer handles.
inline bool sampleNatureField(const World& world, Vec3 position,
                              NatureFieldSample& output) {
    const auto* state = world.resourceConst<State>();
    if (!state) return false;
    NatureFieldSample value;
    value.position = position;
    value.tick = state->tick;
    value.windVelocity = windWithStormsAt(world, position);
    value.gravityAcceleration = gravityAt(world, position);
    value.storms = stormsAt(world, position, state->tick);

    const Entity cellEntity = cellAtPosition(world, position);
    if (const auto* coord = world.tryGetConst<Cell>(cellEntity)) {
        value.hasAtmosphere = environmentAt(world, coord->x, coord->z,
                                             value.environment);
        if (const auto* atmosphere = world.tryGetConst<Atmosphere>(cellEntity))
            value.pressureHpa = atmosphere->pressure;
        if (const auto* hazard = world.tryGetConst<HazardCell>(cellEntity)) {
            value.hazards = *hazard;
            value.hasHazards = true;
        }
    }
    output = value;
    return true;
}

} // namespace aiverse::physis
