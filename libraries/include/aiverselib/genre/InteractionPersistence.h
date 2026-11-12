#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for physical-interactions.
// Record kinds 80..87 are reserved for this package: CarryState 80, PushState 81, RopeState 82, DoorState 83,
// PlatformState 84, BreakableState 85 (86..87 free). Store each state in its own NativeState key; bind
// interactionConfigHash and interactionDefinitionsHash in the save envelope. Loading validates structure only:
// the step functions additionally check every record against the bound definitions.
#include "PhysicalInteractions.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,HeldObject& h){a(h.object,h.position,h.since,h.chargeStart,h.charging);}
inline void io(Archive& a,FlightState& f){a(f.thrower,f.throwerCollider,f.position,f.velocity,f.age);}
inline void io(Archive& a,CarryState& s){a(s.held,s.flights,s.lastTick,s.initialized);}
inline void io(Archive& a,PushBody& b){a(b.position,b.velocity,b.from,b.to,b.actor,b.moveStart,b.moving,b.blocked,b.resisted);}
inline void io(Archive& a,PushState& s){a(s.bodies,s.lastTick,s.initialized);}
inline void io(Archive& a,RopeEnd& e){a(e.anchor,e.point,e.body,e.mass);}
inline void io(Archive& a,RopeInstance& r){a(r.points,r.velocities,r.a,r.b,r.tension,r.broken);}
inline void io(Archive& a,RopeState& s){a(s.ropes,s.lastTick,s.initialized);}
inline void io(Archive& a,DoorInstance& d){a(d.phase,d.openness,d.openedAt,d.actor,d.locked,d.blocked);}
inline void io(Archive& a,DoorState& s){a(s.doors,s.lastTick,s.initialized);}
inline void io(Archive& a,PlatformInstance& p){a(p.position,p.velocity,p.delta,p.target,p.waitUntil,p.direction,p.active,p.waiting,p.finished,p.riders);}
inline void io(Archive& a,PlatformState& s){a(s.platforms,s.lastTick,s.initialized);}
inline void io(Archive& a,BreakableInstance& b){a(b.health,b.stage,b.lastDamage,b.broken);}
inline void io(Archive& a,BreakableState& s){a(s.objects,s.lastTick,s.initialized);}
inline uint16_t stateKind(const CarryState&){return 80;}
inline uint16_t stateKind(const PushState&){return 81;}
inline uint16_t stateKind(const RopeState&){return 82;}
inline uint16_t stateKind(const DoorState&){return 83;}
inline uint16_t stateKind(const PlatformState&){return 84;}
inline uint16_t stateKind(const BreakableState&){return 85;}

// Structural validation used by load (definition-independent).
inline bool valid(const CarryState& s){
    if(s.held.size()>kPhysMaxObjects||s.flights.size()>kPhysMaxObjects)return false;
    std::vector<int64_t> objects;
    for(const auto& [carrier,h]:s.held){if(carrier<=0||h.object<=0||!physBounded(h.position)||h.since>s.lastTick||(h.charging&&h.chargeStart>s.lastTick))return false;objects.push_back(h.object);}
    for(const auto& [object,f]:s.flights){if(object<=0||f.thrower<0||!physBounded(f.position)||!physBounded(f.velocity,kPhysMaxSpeed))return false;objects.push_back(object);}
    std::sort(objects.begin(),objects.end());return std::adjacent_find(objects.begin(),objects.end())==objects.end();
}
inline bool valid(const PushState& s){
    if(s.bodies.size()>kPhysMaxObjects)return false;
    for(const auto& [id,b]:s.bodies)if(id<=0||b.actor<0||!physBounded(b.position)||!physBounded(b.from)||!physBounded(b.to)||!physBounded(b.velocity,kPhysMaxSpeed)||(b.moving&&b.moveStart>s.lastTick))return false;
    return true;
}
inline bool valid(const RopeState& s){
    if(s.ropes.size()>kPhysMaxObjects)return false;
    for(const auto& [id,r]:s.ropes){
        if(id<=0||r.points.size()<2||r.points.size()>kPhysMaxSegments+1||r.velocities.size()!=r.points.size()||!valid(r.a)||!valid(r.b)||r.tension<Fixed{})return false;
        for(std::size_t i=0;i<r.points.size();++i)if(!physBounded(r.points[i])||!physBounded(r.velocities[i],kPhysMaxSpeed))return false;
    }
    return true;
}
inline bool valid(const DoorState& s){
    if(s.doors.size()>kPhysMaxObjects)return false;
    for(const auto& [id,d]:s.doors)if(id<=0||uint8_t(d.phase)>3||d.openness<0||d.openness>int64_t(kPhysMaxPhaseTicks)*kPhysMaxPhaseTicks||d.openedAt>s.lastTick||d.actor<0||(d.phase==DoorPhase::Closed&&d.openness))return false;
    return true;
}
inline bool valid(const PlatformState& s){
    if(s.platforms.size()>kPhysMaxObjects)return false;
    for(const auto& [id,p]:s.platforms)if(id<=0||p.target>=kPhysMaxWaypoints||(p.direction!=1&&p.direction!=-1)||!physBounded(p.position)||!physBounded(p.velocity,kPhysMaxSpeed)
        ||!physBounded(p.delta,kPhysMaxSpeed)||p.riders.size()>kPhysMaxObjects||!physSortedUnique(p.riders)||(!p.riders.empty()&&p.riders.front()<=0))return false;
    return true;
}
inline bool valid(const BreakableState& s){
    if(s.objects.size()>kPhysMaxObjects)return false;
    for(const auto& [id,b]:s.objects)if(id<=0||b.health<0||b.health>kPhysMaxHealth||b.stage<0||b.lastDamage<0||b.broken!=(b.health==0))return false;
    return true;
}

// Canonical Archive bytes folded into a checksum (states, single records).
template<class T> void hashInteraction(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t interactionHash(const T& value){Hasher h;hashInteraction(h,value);return h.value();}
inline uint64_t interactionConfigHash(const InteractionConfig& c){
    Hasher h;
    for(auto f:{c.gravity,c.skin,c.maxMass,c.reach,c.holdDistance,c.holdHeight,c.followSpeed,c.breakDistance,c.throwMinImpulse,c.throwMaxImpulse,c.pushMaxSpeed})h.fx(f);
    for(auto v:{c.speedPenaltyMilli,c.minSpeedMilli,c.ropeDampingMilli})h.i64(v);
    for(auto v:{c.chargeTicks,c.maxFlightTicks,c.gridMoveTicks,c.ropeIterations})h.u32(v);
    return h.value();
}
// Identity of the immutable definitions (bind next to the state records).
inline uint64_t interactionDefinitionsHash(const InteractionDefinitions& d){
    Hasher h;
    for(const auto& [id,x]:d.carryables){h.i64(id);h.u32(x.collider);h.fx(x.mass);h.v3(x.half);h.u32(x.throwable);}
    h.u32(0xC0FFEEu);for(const auto& [id,x]:d.pushables){h.i64(id);h.u32(x.collider);h.fx(x.mass);h.v3(x.half);h.i64(x.frictionMilli);h.u32(x.grid);h.u32(x.pullable);h.fx(x.cell);}
    auto end=[&](const RopeEnd& e){h.u32(uint32_t(e.anchor));h.v3(e.point);h.i64(e.body);h.fx(e.mass);};
    h.u32(0xC0FFEEu);for(const auto& [id,x]:d.ropes){h.i64(id);h.fx(x.length);h.u32(x.segments);h.fx(x.particleMass);h.fx(x.compliance);h.fx(x.breakForce);end(x.a);end(x.b);}
    h.u32(0xC0FFEEu);for(const auto& [id,x]:d.doors){h.i64(id);h.u32(x.collider);h.u32(uint32_t(x.kind));h.v3(x.panelMin);h.v3(x.panelMax);h.v3(x.pivot);h.v3(x.slide);h.fx(x.openAngle);
        h.u32(x.openTicks);h.u32(x.closeTicks);h.u32(x.autoCloseTicks);h.u32(x.locked);h.u32(x.autoReverse);h.i64(x.key);h.i32(x.oneWay);}
    h.u32(0xC0FFEEu);for(const auto& [id,x]:d.platforms){h.i64(id);h.u32(x.collider);h.v3(x.half);h.u32(uint32_t(x.waypoints.size()));for(auto p:x.waypoints)h.v3(p);
        h.u32(uint32_t(x.mode));h.fx(x.speed);h.u32(uint32_t(x.waits.size()));for(auto w:x.waits)h.u32(w);h.u32(x.startActive);}
    auto debris=[&](const std::vector<DebrisSpec>& list){h.u32(uint32_t(list.size()));for(const auto& s:list){h.i64(s.kind);h.u32(s.count);}};
    h.u32(0xC0FFEEu);for(const auto& [id,x]:d.breakables){h.i64(id);h.i64(x.health);h.i64(x.defaultScaleMilli);h.u32(uint32_t(x.damage.size()));
        for(const auto& r:x.damage){h.i64(r.type);h.i64(r.scaleMilli);h.i64(r.threshold);}h.u32(uint32_t(x.stages.size()));for(const auto& s:x.stages){h.i64(s.stage);h.i64(s.health);debris(s.debris);}debris(x.brokenDebris);}
    return h.value();
}
}
