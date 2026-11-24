// Optional native project adapter: automatically hydrates the actual
// GameCore map-spawned ECS entities from Studio-authored World Processes data.
// No engine source change. Other GameModule hosts use hydrateMap() directly.
#pragma once
#include "WorldProcessesMapHydration.h"
#include "ImprintSurfaceTrails.h"
#include "CrowdGameCoreSurface.h"
#include "EvolveSurfaceRegions.h"
#include "GranularSurfaceField.h"
#include "MorphPhysisGravity.h"
#include "AetherPhysisStratification.h"
#include "WorldProcessesPlanetBankPipeline.h"
#include "WorldProcessesPlanetForestPipeline.h"
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
inline bool hydrateGameCoreSurfaceArea(Simulation& sim,Entity e,const Json& doc,std::string* error){
    const auto* host=dynamic_cast<const game::GameCore*>(&sim.module());
    const auto* surface=host?host->surface(sim):nullptr;
    const auto* tr=static_cast<const World&>(sim.world()).tryGetConst<Transform>(e);
    if(!surface||!tr||sim.world().has<evolve::SurfaceArea>(e)||!extended::validate("EvolveArea",doc,error))return false;
    evolve::Area local;
    local.centerXcm=doc.has("offsetXcm")?doc["offsetXcm"].integer():0;
    local.centerZcm=doc.has("offsetZcm")?doc["offsetZcm"].integer():0;
    local.halfWidthCm=doc["halfWidthCm"].integer();
    local.halfDepthCm=doc["halfDepthCm"].integer();
    local.enabled=doc["enabled"].boolean();
    if(!evolve::setSurfaceArea(sim.world(),*surface,e,tr->pos,local)){
        if(error)*error="Invalid planetary region origin or bounds";
        return false;
    }
    return true;
}
inline void installGameCoreSurfaceGranular(Simulation& sim){
    granular::registerPersistence(sim);
    sim.addSystem("granular.settle",[](Simulation& current){
        const auto* host=dynamic_cast<const game::GameCore*>(&current.module());
        if(host)if(const auto* surface=host->surface(current))
            (void)granular::advanceSurfaceFields(current.world(),*surface,current.tick());
    });
}
inline void installGameCoreMorphGravity(Simulation& sim){
    morph::registerPersistence(sim);
    morph::registerGravityPersistence(sim);
    sim.addSystem("morph.constraints",[](Simulation& current){
        const auto* host=dynamic_cast<const game::GameCore*>(&current.module());
        if(host)(void)morph::advanceWithPhysisGravity(current.world(),host->surface(current),current.tick());
    });
}
inline void installGameCoreAetherStratification(Simulation& sim){
    aether::registerPersistence(sim);
    sim.addSystem("aether.transport",[](Simulation& current){
        const auto* host=dynamic_cast<const game::GameCore*>(&current.module());
        if(host)(void)aether::advanceWithStratification(current.world(),host->surface(current),current.tick());
    });
}
class WorldProcessesNativeModule final:public game::NativeModule {
public:
    using PlanetPhysicsInstaller=void(*)(Simulation&,const PlanetForestPipelineConfig&);
    explicit WorldProcessesNativeModule(PlanetPhysicsInstaller install=nullptr):installPhysics_(install){}
    void setup(game::GameCore& host,Simulation& sim,
               game::NativeState&,const Json& config) const override {
        // Project may enable any of the seven new systems even before
        // adding its first authored component. Unknown/typed flags fail fast.
        auto suite=config;
        suite.erase("planetBanks");
        suite.erase("planetForest");
        suite.erase("planetForestPhysics");
        if(config.has("planetForestPhysics")&&config["planetForestPhysics"].type()!=Json::Type::Bool)
            throw std::invalid_argument("planetForestPhysics requires Boolean");
        const bool physics=config.getBool("planetForestPhysics",false);
        if(physics&&(!config.has("planetForest")||!installPhysics_||!sim.config().singleAuthorityPhysics))
            throw std::invalid_argument("planetForestPhysics requires a linked single-authority native host");
        const SuiteFlags flags=suiteFlags(suite);
        if(flags.crowdGravitySupport&&!host.surface(sim))
            throw std::invalid_argument("CROWD gravity support requires planetary Surface");
        const CrowdInstaller crowdInstaller=host.surface(sim)?
            (flags.crowdGravitySupport?&installGameCoreGravitySurfaceCrowd:
                                      &installGameCoreSurfaceCrowd):nullptr;
        (void)authoring::hydrateMap(sim,host.matchMap(),host.mapPlaced(),flags,
            host.surface(sim)?&installGameCoreSurfaceTrails:nullptr,
            crowdInstaller,
            host.surface(sim)?&evolve::registerSurfaceAreaPersistence:nullptr,
            host.surface(sim)?&hydrateGameCoreSurfaceArea:nullptr,
            host.surface(sim)?&installGameCoreSurfaceGranular:nullptr,
            &installGameCoreMorphGravity,
            &installGameCoreAetherStratification);
        if(config.has("planetBanks")){
            if(config.has("planetForest")&&!config["planetBanks"].getBool("rootFalls"))
                throw std::invalid_argument("planetBanks.rootFalls required with planetForest");
            installPlanetBankPipeline(host,sim,config["planetBanks"]);
        }
        if(config.has("planetForest")){
            const auto forest=installPlanetForestPipeline(host,sim,config["planetForest"]);
            if(physics)installPhysics_(sim,forest);
        }
    }
private:
    PlanetPhysicsInstaller installPhysics_=nullptr;
};
inline std::unique_ptr<game::NativeModule> createWorldProcessesNativeModule(){
    return std::make_unique<WorldProcessesNativeModule>();
}
inline game::NativeDefinition worldProcessesNativeDefinition(){
    return {"world_processes",1,&createWorldProcessesNativeModule};
}
} // namespace aiverse::lib::worldprocess
