// Optional native project adapter: automatically hydrates the actual
// GameCore map-spawned ECS entities from Studio-authored World Processes data.
// No engine source change. Other GameModule hosts use hydrateMap() directly.
#pragma once
#include "WorldProcessesMapHydration.h"
#include "ImprintSurfaceTrails.h"
#include "engine/game/GameCore.h"
#include "engine/game/Native.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::lib::worldprocess {

// Register as a standard project NativeDefinition named "world_processes"
// and enable via rules.json {"nativeModules":[{"id":"world_processes"}]}.
// A regular AIVERSE game, server or editor that compiles the project's
// native modules then gets the same deterministic components from its map.
//
// GameCore::setupMap has already created its `mapPlaced()` before native_.load()
// invokes this setup. The Simulation registry is still open at this point.
// This adapter stores no authoritative data outside World and has no genre
// checks. It does not install on world-space HostSystems without mapPlaced().
// Re-resolve the canonical immutable Surface each tick, including after
// rollback. No second legacy/CROWD trace producer on planetary maps.
inline void installGameCoreSurfaceTrails(Simulation& sim){
    sim.registerPersistentComponent<imprint::Tracker>("imprint.tracker",1);
    sim.registerPersistentComponent<imprint::SurfaceTrailHistory>("imprint.surface_history",1);
    sim.addSystem("imprint.surface_trails",[](Simulation& current){
        const auto* host=dynamic_cast<const game::GameCore*>(&current.module());
        if(host)if(const auto* surface=host->surface(current))
            (void)imprint::stampSurfaceMotion(current.world(),*surface,current.tick());
    });
}
class WorldProcessesNativeModule final:public game::NativeModule {
public:
    void setup(game::GameCore& host,Simulation& sim,
               game::NativeState&,const Json& config) const override {
        // Project may enable any of the seven new systems even before
        // adding its first authored component. Unknown/typed flags fail fast.
        const SuiteFlags flags=suiteFlags(config);
        (void)authoring::hydrateMap(sim,host.matchMap(),host.mapPlaced(),flags,
            host.surface(sim)?&installGameCoreSurfaceTrails:nullptr);
    }
};
inline std::unique_ptr<game::NativeModule> createWorldProcessesNativeModule(){
    return std::make_unique<WorldProcessesNativeModule>();
}
inline game::NativeDefinition worldProcessesNativeDefinition(){
    return {"world_processes",1,&createWorldProcessesNativeModule};
}
} // namespace aiverse::lib::worldprocess
