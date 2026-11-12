#pragma once
// Versioned save/load (genre::save/load from Persistence.h, version 1) and checksum hashing for vehicle-controllers.
// Record kinds 88..95 are shared by vehicle-controllers and sports-rules: VehicleState 88, VehicleDamageState 89,
// VehicleSeatState 90, RaceState 91, (sports) SportsState 92, 93 reserved, RaceBotState 94, (sports) SportsBotState 95.
// Store each record in its own NativeState key; feed vehicleHash(state) into the simulation checksum and bind
// vehicleDefinitionHash(def) / raceCourseHash(course) / raceLineHash(line) in the save envelope.
#include "Vehicles.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,VehicleState& s){
    a(s.position,s.velocity,s.yaw,s.yawRate,s.pitch,s.roll,s.steer,s.rpm,s.airspeed,s.sink,s.longAccel,s.trackLeft,s.trackRight);
    a(s.gear,s.shiftUntil,s.slipMask,s.lastTick,s.grounded,s.drifting,s.stalled,s.initialized);
}
inline void io(Archive& a,VehicleZoneState& s){a(s.health,s.destroyed);}
inline void io(Archive& a,VehicleDamageState& s){a(s.zones,s.lastInput,s.lastTick,s.disabled,s.initialized);}
inline void io(Archive& a,VehicleSeatState& s){a(s.occupants,s.team,s.lastRequest,s.lastTick,s.locked,s.initialized);}
inline void io(Archive& a,RaceRacerState& s){a(s.last,s.wrongWayFor,s.slowFor,s.respawns,s.finishTick,s.wrongWay,s.seen);}
inline void io(Archive& a,RaceState& s){a(s.route,s.racers,s.lastTick,s.initialized);}
inline uint16_t stateKind(const VehicleState&){return 88;}
inline uint16_t stateKind(const VehicleDamageState&){return 89;}
inline uint16_t stateKind(const VehicleSeatState&){return 90;}
inline uint16_t stateKind(const RaceState&){return 91;}
// Immutable definitions: hashed through the same canonical encoding, never saved as state records.
inline void io(Archive& a,VehicleCurvePoint& p){a(p.x,p.y);}
inline void io(Archive& a,VehicleWheelDef& w){a(w.offset,w.radius,w.driven,w.steered,w.braked,w.handbrake);}
inline void io(Archive& a,VehicleWheeledConfig& c){
    a(c.mass,c.gravity,c.cgHeight,c.torque,c.idleRpm,c.redlineRpm,c.shiftUpRpm,c.shiftDownRpm,c.gears,c.reverseGear,c.finalDrive,c.efficiency,c.shiftTicks,c.automatic);
    a(c.brakeForce,c.handbrakeForce,c.handbrakeGrip,c.maxSteer,c.steerRate,c.steerFalloffSpeed,c.friction,c.longitudinalGrip,c.lateralGrip,c.drag,c.rolling,c.driftEnter,c.driftExit,c.driftMinSpeed,c.wheels);
}
inline void io(Archive& a,VehicleTrackedConfig& c){a(c.mass,c.gravity,c.maxSpeed,c.reverseSpeed,c.acceleration,c.braking,c.trackWidth,c.lateralGrip,c.drag,c.pivotSpeed);}
inline void io(Archive& a,VehicleHoverConfig& c){a(c.mass,c.gravity,c.height,c.stiffness,c.damping,c.thrust,c.reverseThrust,c.drag,c.lateralDamping,c.turnRate,c.yawResponse);}
inline void io(Archive& a,VehicleFlightConfig& c){
    a(c.mass,c.gravity,c.thrust,c.drag,c.stallSpeed,c.recoverSpeed,c.pitchRate,c.rollRate,c.yawRate,c.maxPitch,c.maxRoll,c.bankTurnRate,c.levelRate,c.stallAuthority,c.stallPitchDrop,c.sinkRecovery,c.groundFriction);
}
inline void io(Archive& a,VehicleControllerDef& d){a(d.kind,d.wheeled,d.tracked,d.hover,d.flight);}
inline void io(Archive& a,VehiclePenalty& p){a(p.stat,p.atZero);}
inline void io(Archive& a,VehicleZoneDef& z){a(z.id,z.center,z.half,z.health,z.armor,z.critical,z.penalties);}
inline void io(Archive& a,VehicleDamageDef& d){a(d.zones);}
inline void io(Archive& a,VehicleSeatDef& s){a(s.id,s.role,s.entry,s.enterRadius,s.exits);}
inline void io(Archive& a,VehicleSeatsDef& d){a(d.seats,d.maxEnterSpeed,d.maxExitSpeed);}
inline void io(Archive& a,VehicleDefinition& d){a(d.id,d.controller,d.damage,d.seats);}
inline void io(Archive& a,RaceCheckpoint& c){a(c.gate.id,c.gate.center,c.gate.normal,c.gate.radius,c.respawn,c.respawnYaw);}
inline void io(Archive& a,RaceGridSlot& g){a(g.position,g.yaw);}
inline void io(Archive& a,RaceCourse& c){a(c.checkpoints,c.laps,c.grid);}
inline void io(Archive& a,RaceConfig& c){a(c.wrongWayTicks,c.wrongWaySpeed,c.respawnWrongWayTicks,c.stuckSpeed,c.stuckTicks);}
// Per-tick outcomes (events, standings, respawns) hash the same way.
inline void io(Archive& a,VehicleEvent& e){a(e.kind,e.subject,e.actor,e.value);}
inline void io(Archive& a,RaceEvent& e){a(e.kind,e.racer,e.value);}
inline void io(Archive& a,RaceStanding& s){a(s.racer,s.place,s.lap,s.nextCheckpoint,s.distanceToNext,s.finished,s.wrongWay);}
inline void io(Archive& a,RaceRespawn& r){a(r.racer,r.position,r.yaw,r.checkpoint);}
inline void io(Archive& a,RaceReport& r){a(r.events,r.standings,r.respawns);}
template<class T> void hashVehicle(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t vehicleHash(const T& value){Hasher h;hashVehicle(h,value);return h.value();}
inline uint64_t vehicleDefinitionHash(const VehicleDefinition& d){return vehicleHash(d);}
inline uint64_t raceCourseHash(const RaceCourse& c){return vehicleHash(c);}
}
namespace aiverse::lib::bots {
inline void io(Archive& a,RaceBotState& s){a(s.segment,s.lastTick,s.lane,s.laneTarget,s.overtaking,s.initialized);}
inline void io(Archive& a,RaceLine& l){a(l.points,l.distance,l.curvature,l.length);}
inline uint16_t stateKind(const RaceBotState&){return 94;}
using genre::save;using genre::load;
template<class T> uint64_t raceBotHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
inline uint64_t raceLineHash(const RaceLine& l){return raceBotHash(l);}
}
