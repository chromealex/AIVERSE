// Native universal gameplay adapter: CIRCUIT devices, SOCIETY NPC events,
// and EVOLVE climate/forest links. Gameplay owners keep physics, lighting,
// plants, dialog, AI, quests and weather; this facade stores only external
// presentation acknowledgement caches, never another canonical simulation.
#pragma once
#include "CircuitDeviceHost.h"
#include "SocietyNpcHost.h"
#include "EvolvePhysisHost.h"
#include "EvolveTerraEcologyBridge.h"
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
    // Feed the actual TerraWeave ForestEcologyClimateSampler; baseline
    // remains the existing PHYSIS/NEREID/biome climate implementation.
    terraweave::ForestEcologyClimateSampler forestClimate(
            const World& ecs,std::vector<EvolvePlantRegion> regions,
            terraweave::ForestEcologyClimateSampler baseline={})const{
        return makeEvolveForestClimateSampler(ecs,std::move(regions),
                                              std::move(baseline));
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
