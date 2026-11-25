#pragma once
#include "aiverselib/world/WorldProcessesNativeModule.h"
#include "engine/physis/PhysisProjectConfig.h"
#if defined(AIVERSE_PLANET_FOREST_PHYSICS_HOST)
#include "aiverselib/world/WorldProcessesPlanetPhysicsHost.h"
#endif
#include <stdexcept>

namespace aiverse::examples::planet_processes {
// Example composition only: PHYSIS owns its fields and their persistence;
// the existing native World Processes adapter owns all process registration.
class PlanetDemoModule final : public game::NativeModule {
public:
    void setup(game::GameCore& host, Simulation& sim,
               game::NativeState& state, const Json& config) const override {
        if (!host.surface(sim) || !physis::installFromMap(sim, host.matchMap()))
            throw std::invalid_argument("Planet demo requires Surface and authored PHYSIS");
        physis::GravitySettings settings;
        settings.baseline = {};
        sim.world().setResource<physis::GravitySettings>(settings);
#if defined(AIVERSE_PLANET_FOREST_PHYSICS_HOST)
        lib::worldprocess::WorldProcessesNativeModule{&lib::worldprocess::installPlanetForestPhysicsAuthority}.setup(host,sim,state,config);
#else
        lib::worldprocess::WorldProcessesNativeModule{}.setup(host, sim, state, config);
#endif
    }
};
inline std::unique_ptr<game::NativeModule> create() {
    return std::make_unique<PlanetDemoModule>();
}
inline game::NativeDefinition definition() {
    return {"world_processes", 1, &create};
}
}
