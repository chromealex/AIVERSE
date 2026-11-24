#pragma once
#include "engine/mechanics/Namespace.h"
// Versioned save records and checksum hashing for battle-royale state. Record kinds 120..127 are reserved
// for this package (120..126 used). Definitions (SafeZoneDef, InsertionFlightDef, ...) are not saved: bind
// royaleDataHash (Data.h) of the loaded JSON in the owner's save envelope.
#include "engine/mechanics/royale/Bombardment.h"
#include "engine/mechanics/royale/Insertion.h"
#include "engine/mechanics/royale/Lobby.h"
#include "engine/mechanics/royale/Standings.h"
#include "engine/mechanics/royale/SupplyDrops.h"
#include "engine/mechanics/royale/SafeZone.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::mechanics {
inline void io(Archive& a,RoyaleCircle& c){a(c.center.x,c.center.y,c.radius);}
inline void io(Archive& a,RoyaleRng& r){a(r.state,r.inc);}
inline void io(Archive& a,SafeZoneState& s){a(s.stage,s.phase,s.stageStart,s.lastTick,s.from,s.to,s.rng,s.initialized);}
inline void io(Archive& a,BombardmentState& s){a(s.stage,s.strikes,s.activeFrom,s.lastTick,s.initialized,s.area,s.rng);}
inline void io(Archive& a,SupplyDrop& d){a(d.id,d.announced,d.lands,d.point.x,d.point.y,d.landed);}
inline void io(Archive& a,SupplyDropState& s){a(s.dropped,s.lastTick,s.falling,s.rng,s.initialized);}
inline void io(Archive& a,InsertionFlight& s){a(s.start,s.end,s.startTick,s.durationTicks,s.initialized);}
inline void io(Archive& a,SkydiverState& s){a(s.phase,s.position,s.velocity,s.phaseTick,s.lastTick,s.initialized);}
inline void io(Archive& a,RoyaleCompetitor& c){a(c.team,c.life,c.kills,c.knocks,c.revives,c.outTick,c.damage,c.knockedBy,c.knockedWeapon);}
inline void io(Archive& a,KillFeedEvent& e){a(e.kind,e.subject,e.actor,e.value,e.tick);}
inline void io(Archive& a,RoyaleTeamStanding& t){a(t.placement,t.outTick);}
inline void io(Archive& a,RoyaleStandings& s){a(s.players,s.teams,s.winner,s.started,s.finished,s.lastTick);}
inline void io(Archive& a,RoyaleLobbyState& s){a(s.stage,s.opened,s.countdownFrom,s.lastTick,s.initialized);}
inline uint16_t royaleKind(const SafeZoneState&){return 120;}
inline uint16_t royaleKind(const BombardmentState&){return 121;}
inline uint16_t royaleKind(const SupplyDropState&){return 122;}
inline uint16_t royaleKind(const InsertionFlight&){return 123;}
inline uint16_t royaleKind(const SkydiverState&){return 124;}
inline uint16_t royaleKind(const RoyaleStandings&){return 125;}
inline uint16_t royaleKind(const RoyaleLobbyState&){return 126;}
template<class State> concept RoyaleRecord=requires(Archive& a,State& s){royaleKind(s);io(a,s);valid(s);};
inline constexpr uint32_t kRoyaleRecordVersion=1;
template<RoyaleRecord State> std::string royaleSave(State s){
    if(!valid(s))return {};
    std::string bytes;Archive a(&bytes);uint32_t version=kRoyaleRecordVersion;auto kind=royaleKind(s);a(version,kind,s);return bytes;
}
// Rejects other kinds / versions, truncated or trailing bytes and states that fail validation.
template<RoyaleRecord State> bool royaleLoad(const std::string& bytes,State& state){
    if(bytes.size()>4*1024*1024)return false;
    Archive a(bytes);uint32_t version=0;uint16_t kind=0;State copy;a(version,kind);
    if(!a.ok()||version!=kRoyaleRecordVersion||kind!=royaleKind(copy))return false;
    a(copy);if(!a.ok()||a.remaining()||!valid(copy)||royaleSave(copy)!=bytes)return false;
    state=std::move(copy);return true;
}
template<RoyaleRecord State> void hashRoyale(Hasher& h,const State& s){std::string bytes;Archive a(&bytes);auto copy=s;auto kind=royaleKind(copy);a(kind,copy);h.str(bytes);}
template<RoyaleRecord State> uint64_t royaleHash(const State& s){Hasher h;hashRoyale(h,s);return h.value();}
}
