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
inline void registerBuildingLiftCheckpointPersistence(Simulation& sim){
    sim.registerPersistentComponent<BuildingLiftPassenger>("building.lift.passenger",1);
    sim.registerPersistentResource<BuildingLiftWorldCheckpoints>("building.lift.checkpoints",1);
    sim.world().setResource<BuildingLiftWorldCheckpoints>();
}
}
