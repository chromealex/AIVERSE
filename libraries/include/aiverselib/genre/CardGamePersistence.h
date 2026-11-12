#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for card-games state.
// Record kinds 96..103 are reserved for the cards+turns batch: CardGameState 96 (97 reserved for cards),
// TacticsState 98 and TacticsOrders 99 (TacticsPersistence.h), 100..103 reserved. card-runtime's own
// strategy/Persistence.h records (CardState kind 4) are unchanged. Store each state in its own NativeState key,
// feed cardGameHash(state) into the simulation checksum and bind cardLibraryHash(library) in the save envelope.
#include "CardGameRunner.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,CardTargetRef& t){a(t.type,t.value);}
inline void io(Archive& a,CardGameCard& c){a(c.id,c.def,c.owner,c.zone,c.known,c.damage,c.bonus,c.exhausted);}
inline void io(Archive& a,CardGameSeat& s){a(s.health,s.energy,s.maxEnergy,s.fatigue,s.turnsTaken,s.eliminated,s.mulliganed,s.deck,s.hand,s.board,s.discard,s.exile);}
inline void io(Archive& a,CardStackItem& f){a(f.id,f.kind,f.source,f.controller,f.target);}
inline void io(Archive& a,CardGameState& s){a(s.seats,s.cards,s.stack,s.phase,s.turn,s.nextStackId,s.active,s.priority,s.passes,s.winner,s.lastCommand);}
inline uint16_t stateKind(const CardGameState&){return 96;}
// Definitions, views, actions and events hash through the same canonical encoding (never saved as records).
inline void io(Archive& a,CardEffectDef& e){a(e.op,e.amount,e.to);}
inline void io(Archive& a,CardDef& d){a(d.id,d.name,d.kind,d.cost,d.power,d.health,d.target,d.side,d.guard,d.rush,d.effects);}
inline void io(Archive& a,CardRules& r){a(r.seats,r.startingHand,r.maxHand,r.startingHealth,r.energyStart,r.energyGain,r.energyMax,r.drawPerTurn,r.boardLimit,r.turnLimit,r.mulliganPenalty);
    a(r.deckMin,r.deckMax,r.firstPlayerSkipsDraw,r.fatigue,r.separateCombat,r.unitsAttackImmediately,r.mulligan);}
inline void io(Archive& a,CardLibrary& l){a(l.rules,l.cards);}
inline void io(Archive& a,CardAction& x){a(x.kind,x.card,x.target,x.replaceMask);}
inline void io(Archive& a,CardEvent& e){a(e.kind,e.player,e.card,e.def,e.target,e.value,e.visible);}
inline void io(Archive& a,CardViewCard& c){a(c.id,c.def,c.owner,c.power,c.health,c.exhausted);}
inline void io(Archive& a,CardSeatView& s){a(s.health,s.energy,s.maxEnergy,s.fatigue,s.eliminated,s.mulliganed,s.deckCount,s.handCount,s.hand,s.board,s.discard,s.exile);}
inline void io(Archive& a,CardStackView& f){a(f.id,f.kind,f.source,f.controller,f.target);}
inline void io(Archive& a,CardGameView& v){a(v.viewer,v.phase,v.turn,v.active,v.priority,v.passes,v.winner,v.seats,v.stack);}
template<class T> uint64_t cardGameHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
inline uint64_t cardLibraryHash(const CardLibrary& lib){return cardGameHash(lib);}
}
