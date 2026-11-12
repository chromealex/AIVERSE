#pragma once
// Versioned save/load (genre::save/load) and hashing for tactics-grid state. Record kinds (cards+turns batch range
// 96..103): TacticsState 98, TacticsOrders 99 (WeGo orders collected but not yet resolved). turn-scheduler's
// strategy/Persistence.h TurnState record (kind 3) is unchanged. Feed tacticsHash(state) (and the pending orders)
// into the simulation checksum and bind tacticsScenarioHash(scenario) in the save envelope.
#include "TacticsWeGo.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,TacticsSpeedModifier& m){a(m.tag,m.delta,m.untilRound);}
inline void io(Archive& a,TacticsUnit& u){a(u.id,u.def,u.team,u.cell,u.hp,u.ap,u.alive,u.delayed,u.speed);}
inline void io(Archive& a,TacticsReservation& r){a(r.unit,r.spent);}
inline void io(Archive& a,TacticsState& s){a(s.units,s.order,s.cursor,s.round,s.reservations,s.winner,s.lastCommand);}
inline void io(Archive& a,TacticsOrder& o){a(o.unit,o.moveTo,o.attack,o.overwatch);}
inline void io(Archive& a,TacticsOrders& o){a(o.round,o.teams);}
inline uint16_t stateKind(const TacticsState&){return 98;}
inline uint16_t stateKind(const TacticsOrders&){return 99;}
// Definitions, views and events hash through the same canonical encoding (never saved as records).
inline void io(Archive& a,TacticsCell& c){a(c.cost,c.cover,c.blocksSight);}
inline void io(Archive& a,TacticsGrid& g){a(g.shape,g.width,g.height,g.cells);}
inline void io(Archive& a,TacticsFireRules& f){a(f.halfCoverMilli,f.fullCoverMilli,f.flankBonusMilli,f.rangePenaltyMilli,f.overwatchPenaltyMilli,f.minHitMilli,f.maxHitMilli,f.coverCosMilli);}
inline void io(Archive& a,TacticsRules& r){a(r.fire,r.roundLimit,r.attackEndsTurn);}
inline void io(Archive& a,TacticsUnitDef& d){a(d.id,d.name,d.hp,d.ap,d.mobility,d.aimMilli,d.damage,d.flankDamage,d.optimalRange,d.maxRange,d.sight,d.initiative);}
inline void io(Archive& a,TacticsScenario& s){a(s.grid,s.rules,s.defs);}
inline void io(Archive& a,TacticsEvent& e){a(e.kind,e.unit,e.target,e.cell,e.value);}
inline void io(Archive& a,TacticsUnitView& u){a(u.id,u.def,u.team,u.cell,u.hp,u.ap,u.overwatch);}
inline void io(Archive& a,TacticsView& v){a(v.team,v.round,v.active,v.winner,v.units,v.order);}
template<class T> uint64_t tacticsHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
inline uint64_t tacticsScenarioHash(const TacticsScenario& s){return tacticsHash(s);}
}
