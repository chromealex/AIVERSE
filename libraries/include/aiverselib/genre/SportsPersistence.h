#pragma once
// Versioned save/load (genre::save/load, version 1) and checksum hashing for sports-rules.
// Record kinds (shared range 88..95 with vehicle-controllers): SportsState 92, SportsBotState 95; 93 is reserved.
// Store each record in its own NativeState key, save the RNG state (Random::state/inc) next to it, feed
// sportsHash(state) into the checksum and bind sportsDefinitionsHash(field, rules, formations) in the save envelope.
#include "Sports.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,SportsBall& b){a(b.position.x,b.position.y,b.velocity.x,b.velocity.y,b.owner,b.lastTouch,b.lastTeam,b.kicker,b.kickedAt,b.ownedSince);}
inline void io(Archive& a,SportsState& s){
    a(s.teams[0],s.teams[1],s.score[0],s.score[1],s.ball,s.phase,s.restart,s.restartTeam,s.restartSpot.x,s.restartSpot.y,s.restartAt);
    a(s.period,s.elapsed,s.stoppage,s.lastTick,s.overtime,s.initialized,s.winner,s.stunned);
}
inline uint16_t stateKind(const SportsState&){return 92;}
// Definitions (hash only).
inline void io(Archive& a,SportsField& f){
    a(f.halfLength,f.halfWidth,f.goalHalfWidth,f.penaltyDepth,f.penaltyHalfWidth,f.penaltySpot,f.goalKickDepth,f.boundary);
    uint32_t n=uint32_t(f.faceoffSpots.size());a(n);if(!a.writing())f.faceoffSpots.resize(n);for(auto& p:f.faceoffSpots)a(p.x,p.y);
}
inline void io(Archive& a,SportsRulesConfig& c){
    a(c.periods,c.periodTicks,c.overtimeTicks,c.restartDelayTicks,c.maxStoppageTicks,c.clock,c.swapEnds,c.goldenGoal,c.pickupRadius,c.controlSpeed,c.dribbleOffset,c.friction,c.restitution);
    a(c.tackleRange,c.tackleBaseMilli,c.interceptMilli,c.behindFoulMilli,c.failFoulMilli,c.faceoffBiasMilli,c.tackleCooldownTicks,c.possessionLockTicks,c.kickLockTicks);
    a(c.passSpeedMin,c.passSpeedMax,c.shotSpeedMin,c.shotSpeedMax,c.passError,c.shotError);
}
inline void io(Archive& a,SportsSlot& s){a(s.id,s.role,s.base.x,s.base.y,s.attack.x,s.attack.y,s.defend.x,s.defend.y,s.ballPull);}
inline void io(Archive& a,SportsFormation& f){a(f.id,f.slots);}
inline void io(Archive& a,SportsEvent& e){a(e.kind,e.team,e.player,e.value,e.extra);}
inline void io(Archive& a,SportsReport& r){a(r.events);}
template<class T> void hashSports(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t sportsHash(const T& value){Hasher h;hashSports(h,value);return h.value();}
inline uint64_t sportsDefinitionsHash(const SportsField& f,const SportsRulesConfig& c,std::span<const SportsFormation> formations){
    Hasher h;hashSports(h,f);hashSports(h,c);for(const auto& x:formations)hashSports(h,x);return h.value();
}
}
namespace aiverse::lib::bots {
inline void io(Archive& a,SportsBotState& s){a(s.slots,s.holder,s.holderSince,s.nextDecision,s.lastTick,s.phase,s.initialized);}
inline uint16_t stateKind(const SportsBotState&){return 95;}
using genre::save;using genre::load;
template<class T> uint64_t sportsBotHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
}
