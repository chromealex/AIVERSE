#pragma once
// Versioned save/load (genre::save/load, version 1) and checksum hashing for genre bot memory.
// Record kinds 56..63 are reserved for genre-bots: 56 MeleeBotState, 57 ShooterBotState, 58 SquadState,
// 59..63 free for later bot adapters. Store each record in its own NativeState key (storeBytes) next to
// the simulation and bind the immutable bot configs/attack sets/weapon definitions in the save envelope.
#include "MeleeBot.h"
#include "ShooterBot.h"
#include "SquadRoles.h"
#include "../genre/Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::bots {
inline void io(Archive& a,MeleeBotState& s){a(s.target,s.threatAttack,s.noticedAt,s.threatStarted,s.lastDecision,s.lastTick,s.move,s.defense,s.threatRead,s.decided,s.initialized);}
inline void io(Archive& a,ShooterBotState& s){a(s.target,s.cover,s.noticedAt,s.lastDecision,s.lastTick,s.errorYaw,s.errorPitch,s.decision,s.cooling,s.decided,s.initialized);}
inline void io(Archive& a,RoleSlot& s){a(s.id,s.role);}
inline void io(Archive& a,SquadState& s){a(s.composition,s.lastAssign,s.lastTick,s.heading.x,s.heading.y,s.initialized,s.roles);}
inline uint16_t stateKind(const MeleeBotState&){return 56;}
inline uint16_t stateKind(const ShooterBotState&){return 57;}
inline uint16_t stateKind(const SquadState&){return 58;}
using genre::save;using genre::load;using runtime::storeBytes;using runtime::loadBytes;
// Folds the canonical record (kind + body, the same bytes as the save body) into the simulation checksum.
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
void hashGenreBot(Hasher& h,const State& s){std::string bytes;Archive a(&bytes);auto copy=s;uint16_t kind=stateKind(copy);a(kind,copy);h.str(bytes);}
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
uint64_t genreBotHash(const State& s){Hasher h;hashGenreBot(h,s);return h.value();}
}
