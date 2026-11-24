#pragma once
// Checkpoint archive adapters for the existing Simulation persistent ECS
// registry. Only server/local saves, never the client snapshot wire protocol.
#include "engine/buildings/BuildingCirculationHost.h"
#include "engine/buildings/BuildingLiftEcsBridge.h"
#include "engine/sim/Simulation.h"
namespace aiverse::lib::systems {
inline void io(Archive& a,BuildingLiftCheckpoint& c){
    auto& s=c.state;
    a(s.featureIndex,s.phase,s.currentFloor,s.targetFloor,s.platformYmm,
      s.phaseElapsedMs,s.revision,s.passengerCount,c.travelRemainder);
    if(!s.revision||s.phase>BuildingLiftPhase::Offline||
       s.passengerCount>BuildingLiftController::kMaxPassengers||
       c.travelRemainder<0||c.travelRemainder>=1000)a.fail();
}
}
namespace aiverse::building {
inline void io(Archive& a,BuildingCirculationCheckpoint& c){
    a(c.lifts,c.riders);
    if(c.lifts.size()>64||c.riders.size()>512)a.fail();
}
struct BuildingLiftWorldCheckpoints {
    std::map<uint64_t,BuildingCirculationCheckpoint> scenes;
};
inline void io(Archive& a,BuildingLiftWorldCheckpoints& c){
    a(c.scenes);
    if(c.scenes.size()>256||c.scenes.contains(0))a.fail();
}
// Validate the decoded checkpoint against actual ECS entity generations
// before Simulation commits a native persistent load.
inline bool validateBuildingLiftCheckpoints(const World& world,std::string& error){
    const auto* all=world.resourceConst<BuildingLiftWorldCheckpoints>();
    if(!all){error="Missing lift checkpoint resource";return false;}
    std::map<uint64_t,std::pair<uint64_t,uint32_t>> bindings;
    for(const auto& [scene,cp]:all->scenes){
        std::map<uint32_t,uint32_t> counts;
        for(const auto& [actor,feature]:cp.riders){
            const auto e=Entity::fromPacked(actor);
            const auto* marker=world.tryGetConst<BuildingLiftPassenger>(e);
            const auto it=cp.lifts.find(feature);
            if(!world.alive(e)||!marker||!marker->attached||marker->sceneId!=scene||
               marker->liftFeature!=feature||it==cp.lifts.end()||
               marker->lastAppliedRevision>it->second.state.revision||
               !bindings.emplace(actor,std::make_pair(scene,feature)).second){
                error="Lift rider checkpoint differs from ECS";return false;
            }
            ++counts[feature];
        }
        for(const auto& [feature,lift]:cp.lifts)
            if(lift.state.featureIndex!=feature||lift.state.passengerCount!=counts[feature]){
                error="Lift passenger count or feature mismatch";return false;
            }
    }
    bool valid=true;
    world.eachExistingConst<BuildingLiftPassenger>([&](Entity e,const BuildingLiftPassenger& marker){
        if(!marker.attached)return;
        auto it=bindings.find(e.packed());
        if(it==bindings.end()||it->second!=std::make_pair(marker.sceneId,marker.liftFeature))
            valid=false;
    });
    if(!valid)error="Orphaned ECS lift passenger";
    return valid;
}
inline void registerBuildingLiftCheckpointPersistence(Simulation& sim){
    sim.registerPersistentComponent<BuildingLiftPassenger>("building.lift.passenger",1);
    sim.registerPersistentResource<BuildingLiftWorldCheckpoints>("building.lift.checkpoints",1);
    sim.world().setResource<BuildingLiftWorldCheckpoints>();
    sim.addPersistentLoadValidator("building.lift.checkpoints",validateBuildingLiftCheckpoints);
}
}
