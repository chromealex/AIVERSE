#pragma once
// Native persistent ECS state of the fixed-point building owners.
// No raw pointers, GPU handles, Box3D float state or transport sockets.
#include "engine/buildings/BuildingEcsRuntime.h"
#include "engine/sim/Simulation.h"
#include <map>
#include <string>
#include <vector>
namespace aiverse::building {
inline void io(Archive& a,BuildingLeafMotionCheckpoint& c){
    a(c.socketId,c.poseRevision,c.domains,c.targetOpen,c.moving);
    if(c.socketId.empty()||c.socketId.size()>128||(c.domains&~kBuildingGraphAll))a.fail();
}
inline void io(Archive& a,BuildingApertureCommand& c){
    a(c.sceneId,c.actor,c.sequence,c.socketId,c.open,c.graphDomains,c.authenticatedPlayer);
    if(!c.sceneId||!c.actor.valid()||!c.sequence||c.socketId.empty()||
       c.socketId.size()>128||!c.graphDomains||(c.graphDomains&~kBuildingGraphAll))a.fail();
}
inline void io(Archive& a,BuildingDualLeafSessionCheckpoint& c){
    a(c.lastTick,c.accepted,c.queue);
    if(c.accepted.size()>65536||c.queue.size()>1024)a.fail();
}
struct BuildingLockstepCommand {Command value;};
inline void io(Archive& a,BuildingLockstepCommand& c){
    auto& v=c.value;
    a(v.tick,v.player,v.flags,v.type,v.seq,v.units,v.target,v.point,v.param,v.args);
    if(!v.player||!v.seq||v.units.size()!=1||v.args.size()!=4||v.param<2||v.param>5)a.fail();
}
struct BuildingLockstepSceneState {
    std::vector<BuildingLeafMotionCheckpoint> motion;
    BuildingDualLeafSessionCheckpoint session;
    std::map<uint64_t,uint64_t> liftSequences;
    std::vector<BuildingLockstepCommand> liftQueue;
    uint64_t navRevision=0;
};
inline void io(Archive& a,BuildingLockstepSceneState& c){
    a(c.motion,c.session,c.liftSequences,c.liftQueue,c.navRevision);
    if(c.motion.size()>64||c.liftSequences.size()>65536||c.liftQueue.size()>1024)a.fail();
}
struct BuildingLockstepWorldState {
    std::map<uint64_t,BuildingLockstepSceneState> scenes;
};
inline void io(Archive& a,BuildingLockstepWorldState& c){
    a(c.scenes);if(c.scenes.size()>256||c.scenes.contains(0))a.fail();
}
inline void registerBuildingLockstepPersistence(Simulation& sim){
    sim.registerPersistentResource<BuildingLockstepWorldState>("building.lockstep",1);
    sim.world().setResource<BuildingLockstepWorldState>();
}
}
