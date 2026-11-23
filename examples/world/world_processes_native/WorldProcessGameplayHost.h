// Native universal gameplay adapter: CIRCUIT devices, SOCIETY NPC events,
// and EVOLVE climate/forest links. Gameplay owners keep physics, lighting,
// plants, dialog, AI, quests and weather; this facade stores only external
// presentation acknowledgement caches, never another canonical simulation.
#pragma once
#include "CircuitDeviceHost.h"
#include "SocietyNpcHost.h"
#include "EvolvePhysisHost.h"
#include "EvolveTerraEcologyBridge.h"
#include "aiverselib/world/EvolveSpatialIndex.h"
#include "engine/world/TerraWeaveForestEcologyEcs.h"
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace aiverse::examples::world_processes_native {
class WorldProcessGameplayHost {
public:
    CircuitDeviceHost& circuitDevices(){return circuits_;}
    SocietyNpcHost& npcReactions(){return society_;}
    const CircuitDeviceHost& circuitDevices()const{return circuits_;}
    const SocietyNpcHost& npcReactions()const{return society_;}
    // A project invokes this at the authoritative command barrier, before
    // circuit.switch_inputs. The Switch and circuit Node remain in World.
    bool setSwitch(World& ecs,Entity switchEntity,bool closed){
        return circuit::setSwitch(ecs,switchEntity,closed);
    }
    // Both device and NPC events are derived from a COMPLETED
    // Simulation::step tick. Physics owner must provide its real Scene.
    bool reconcile(const World& ecs,std::uint32_t completedTick,
                   physics::Scene* physicsScene=nullptr,bool force=false){
        if(!circuits_.reconcile(ecs,completedTick,physicsScene,force))
            return false;
        return society_.reconcile(ecs,completedTick,force);
    }
    // Attach the canonical PHYSIS atmosphere+surface to an EVOLVE region
    // after the owner has actually spawned all ECS components.
    bool attachClimate(World& ecs,Entity linkEntity,Entity region,
                       Entity physisCell,evolve::ClimateLink settings,
                       std::string* error=nullptr){
        return attachEvolveClimate(ecs,linkEntity,region,physisCell,
                                  settings,error);
    }
    // Preferred route: read the authoritative EvolveArea ECS rectangles
    // hydrated from Studio-authored map positions, instead of manually
    // maintaining a parallel native host region list.
    terraweave::ForestEcologyClimateSampler forestClimate(
            const World& ecs,
            terraweave::ForestEcologyClimateSampler baseline={})const{
        return makeEvolveForestClimateSampler(ecs,std::move(baseline));
    }
    // Feed the actual TerraWeave ForestEcologyClimateSampler; baseline
    // remains the existing PHYSIS/NEREID/biome climate implementation.
    terraweave::ForestEcologyClimateSampler forestClimate(
            const World& ecs,std::vector<EvolvePlantRegion> regions,
            terraweave::ForestEcologyClimateSampler baseline={})const{
        return makeEvolveForestClimateSampler(ecs,std::move(regions),
                                              std::move(baseline));
    }
    // Synchronous ecology entry point: rebuild the region identity snapshot
    // from the completed ECS tick on every call, including after rollback.
    // TerraWeave owns the forest resource and its transactional update.
    terraweave::ForestEcologyResult advanceEcology(
            const World& ecs,
            terraweave::ForestEcologyResource& forest,
            std::vector<terraweave::AnchoredPlant>& rooted,
            std::uint64_t completedTick,
            const std::vector<terraweave::ForestEcologySpecies>& species,
            const terraweave::ForestEcologySoilSampler& soilHeight,
            terraweave::ForestEcologyClimateSampler baseline={},
            terraweave::ForestEcologySettings settings={})const{
        return terraweave::advanceForestEcology(
            forest,rooted,completedTick,species,soilHeight,
            forestClimate(ecs,std::move(baseline)),settings);
    }
    // Preferred authoritative path: TerraWeave already persists its forest
    // history and rooted population through World COW. No extra checkpoint.
    static terraweave::ForestEcologyEcsOutcome advanceEcologyEcs(
            World& ecs,const terraweave::TerraWeaveEcsAuthorityConfig& cfg,
            std::uint64_t tick,
            const std::vector<terraweave::ForestEcologySpecies>& species,
            terraweave::ForestEcologySettings settings={},
            terraweave::ForestEcologyClimateOverride baseline={}){
        const evolve::SpatialIndex zones(ecs);
        return terraweave::advanceForestEcologyEcs(
            ecs,cfg,tick,species,settings,
            [&ecs,&zones,baseline=std::move(baseline),scratch=std::vector<size_t>{}](
                const terraweave::AnchoredPlant& plant,
                terraweave::ForestEcologyClimate climate) mutable {
                if(baseline)climate=baseline(plant,climate);
                zones.query(plant.anchor.xCm,plant.anchor.zCm,scratch,[&](const evolve::Extent& zone){
                    const auto* region=ecs.tryGetConst<evolve::Region>(zone.region);
                    if(region)climate=projectEvolutionClimate(*region,climate);
                });
                return climate;
            });
    }
    // Register after EVOLVE and existing TerraWeave authority/water systems.
    // Persistence registration remains with the TerraWeave owner.
    static void addEcologyEcsSystem(
            Simulation& sim,std::string name,
            terraweave::TerraWeaveEcsAuthorityConfig cfg,
            std::vector<terraweave::ForestEcologySpecies> species,
            terraweave::ForestEcologySettings settings={},
            terraweave::ForestEcologyClimateOverride baseline={}){
        sim.addSystem(std::move(name),
            [cfg=std::move(cfg),species=std::move(species),settings,
             baseline=std::move(baseline)](Simulation& current){
                advanceEcologyEcs(current.world(),cfg,current.tick(),
                                  species,settings,baseline);
            });
    }
    // Legacy standalone caller-owned checkpoint, not for ECS authority.
    // Caller-owned checkpoint: forest state and roots must travel together.
    struct EcologyCheckpoint {
        std::uint64_t completedTick=0;
        terraweave::ForestEcologyResource forest;
        std::vector<terraweave::AnchoredPlant> rooted;
    };
    static EcologyCheckpoint captureEcology(
            std::uint64_t completedTick,
            const terraweave::ForestEcologyResource& forest,
            const std::vector<terraweave::AnchoredPlant>& rooted){
        terraweave::validateForestEcologyResource(forest);
        if(forest.initialized&&forest.lastStepTick>completedTick)
            throw std::invalid_argument("EVOLVE ecology checkpoint tick mismatch");
        return {completedTick,forest,rooted};
    }
    void restoreEcology(const EcologyCheckpoint& checkpoint,
            std::uint64_t restoredTick,
            terraweave::ForestEcologyResource& forest,
            std::vector<terraweave::AnchoredPlant>& rooted){
        if(checkpoint.completedTick!=restoredTick)
            throw std::invalid_argument("EVOLVE ecology rollback tick mismatch");
        auto staged=captureEcology(restoredTick,checkpoint.forest,checkpoint.rooted);
        forest=std::move(staged.forest);
        rooted=std::move(staged.rooted);
        resetAfterRollback();
    }
    // Rollback invalidates only transient host deduplication, not ECS.
    void resetAfterRollback(){
        circuits_.resetAfterRollback();
        society_.resetAfterRollback();
    }
private:
    CircuitDeviceHost circuits_;
    SocietyNpcHost society_;
};
} // namespace aiverse::examples::world_processes_native
