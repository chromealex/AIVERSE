#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for stealth-senses state.
// Record kinds 48..55 are reserved for this package (48..52 used). Store SensesState in its own NativeState
// key and bind sensesConfigHash plus the disguise/hiding definition version in the save envelope.
#include "Senses.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,AwarenessRecord& r){a(r.meter,r.level,r.lastKnown,r.lastKnownTick,r.lastStimulus,r.lastSense);}
inline void io(Archive& a,SearchPoint& p){a(p.candidate,p.spot,p.position);}
inline void io(Archive& a,SearchState& s){a(s.active,s.target,s.lastKnown,s.points,s.next,s.startedAt,s.deadline);}
inline void io(Archive& a,ObserverState& s){a(s.targets,s.search);}
inline void io(Archive& a,TrailMarker& m){a(m.id,m.owner,m.team,m.position,m.placedAt,m.strengthMilli);}
inline void io(Archive& a,PendingAlert& p){a(p.deliverAt,p.sentAt,p.to,p.from,p.target,p.position);}
inline void io(Archive& a,DisguiseState& d){a(d.disguise,d.since,d.blown);}
inline void io(Archive& a,HidingState& h){a(h.spot,h.enteredAt,h.revealedTo);}
inline void io(Archive& a,SensesState& s){a(s.observers,s.markers,s.nextMarker,s.alerts,s.disguises,s.hiding,s.lastTick,s.initialized);}
inline uint16_t stateKind(const SensesState&){return 48;}
inline uint16_t stateKind(const ObserverState&){return 49;}
inline uint16_t stateKind(const SearchState&){return 50;}
inline uint16_t stateKind(const DisguiseState&){return 51;}
inline uint16_t stateKind(const HidingState&){return 52;}

inline void io(Archive& a,PerceivedTarget& p){a(p.observer,p.target,p.level,p.suspicion,p.lastKnown,p.lastKnownTick,p.lastSense,p.sensedNow,p.visibleNow,p.strengthNow,p.searching);}
inline void io(Archive& a,SenseEvent& e){a(e.kind,e.observer,e.target,e.value,e.extra);}
inline void io(Archive& a,SensesReport& r){a(r.perceived,r.events,r.losQueries,r.losSkipped,r.occlusionQueries,r.occlusionSkipped,r.noisesDropped);}
// Canonical Archive bytes (the save body) folded into a checksum; works for states and per-tick reports.
template<class T> void hashSenses(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t sensesHash(const T& value){Hasher h;hashSenses(h,value);return h.value();}
inline uint64_t sensesConfigHash(const SensesConfig& c){
    Hasher h;const auto& s=c.sight;const auto& n=c.hearing;const auto& t=c.trail;const auto& m=c.suspicion;const auto& a=c.alert;const auto& q=c.search;
    for(auto f:{s.nearRange,s.range,s.halfAngle,s.peripheralRange,s.peripheralHalfAngle,n.maxRadius,t.range,a.radius,q.pointRadius,q.candidateRadius,c.hiding.enterRange})h.fx(f);
    for(auto v:{s.nearMilli,s.farMilli,s.peripheralMilli,s.minMilli,n.thresholdMilli,t.minMilli,m.maximum,m.suspiciousAt,m.searchingAt,m.alertAt,m.sightGain,m.hearingGain,m.trailGain,m.decayPerTick,a.receivedMilli})h.i64(v);
    for(auto v:{s.maxQueries,n.maxNoises,n.maxQueries,t.lifetime,t.maxMarkers,m.decayDelay,a.delay,a.maxPending,q.giveUpTicks,q.maxPoints,uint32_t(a.relay)})h.u32(v);
    return h.value();
}
}
