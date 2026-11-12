#pragma once
#include "Cards.h"
#include "ZoneGraph.h"
#include "Rhythm.h"
#include "RunGraph.h"
#include "PuzzleMachine.h"
#include "Turns.h"
#include "Puzzles.h"
#include "Offline.h"
#include "Production.h"
#include "engine/core/Archive.h"
#include "../runtime/NativeBytes.h"

namespace aiverse::lib::strategy {
inline void io(Archive& a,BigNumber& n){auto text=n.decimal();a(text);if(!a.writing()){auto v=BigNumber::parse(text);if(!v||v->decimal()!=text){a.fail();return;}n=std::move(*v);}}
inline void io(Archive& a,TurnActor& s){a(s.id,s.owner,s.initiative,s.maxAp,s.ap,s.alive);}
inline void io(Archive& a,TurnAction& s){a(s.sequence,s.actor,s.target,s.kind,s.cost,s.reaction);}
inline void io(Archive& a,TurnState& s){uint64_t cursor=s.cursor;a(s.actors,s.order,s.pending,s.round,s.lastSequence,cursor);if(!a.writing()){if(cursor>1024){a.fail();return;}s.cursor=size_t(cursor);}}
inline void io(Archive& a,CardInstance& s){a(s.id,s.definition,s.owner);}
inline void io(Archive& a,CardSeat& s){a(s.health,s.guard,s.energy);for(auto& z:s.zones)a(z);}
inline void io(Archive& a,CardFrame& s){a(s.card,s.caster,s.target);}
inline void io(Archive& a,CardState& s){a(s.seats,s.cards,s.stack,s.active,s.priority,s.passes,s.turn,s.lastCommand);}
inline void io(Archive& a,PushState& s){a(s.player,s.boxes);}
inline void io(Archive& a,PushHistory& s){a(s.current,s.past,s.future);}
inline void io(Archive& a,OfflineState& s){a(s.tick,s.amounts);}
inline void io(Archive& a,FactoryNode& s){a(s.id,s.stock,s.capacity);}
inline void io(Archive& a,FactoryState& s){a(s.tick,s.nodes);}
inline void io(Archive& a,ZoneState& s){a(s.zone,s.checkpoint,s.abilities,s.visited,s.lastCommand);}
inline void io(Archive& a,RhythmState& s){a(s.time,s.calibration,s.lastCommand,s.score,s.combo,s.maxCombo,s.held,s.results,s.heads);}
inline void io(Archive& a,RunRoom& s){a(s.layer,s.cost,s.encounter,s.offers);}
inline void io(Archive& a,RunState& s){a(s.rooms,s.edges,s.path,s.claims,s.phase,s.lastCommand);}
inline void io(Archive& a,MachineState& s){for(auto& v:s.registers)a(v);a(s.memory,s.input,s.output,s.pc,s.inputCursor,s.steps,s.halted,s.fault);}
inline bool valid(const BigNumber&){return true;}
inline bool valid(const PushState& s){return s.player>=0&&s.player<4096&&!s.boxes.empty()&&s.boxes.size()<=256&&std::is_sorted(s.boxes.begin(),s.boxes.end())&&s.boxes.front()>=0&&s.boxes.back()<4096&&std::adjacent_find(s.boxes.begin(),s.boxes.end())==s.boxes.end()&&!std::binary_search(s.boxes.begin(),s.boxes.end(),s.player);}
inline bool valid(const PushHistory& s){if(!valid(s.current)||s.past.size()>4096||s.future.size()>4096)return false;for(const auto* states:{&s.past,&s.future})for(const auto& v:*states)if(!valid(v))return false;return true;}
inline uint16_t kind(const BigNumber&){return 1;}
inline uint16_t kind(const OfflineState&){return 2;}
inline uint16_t kind(const TurnState&){return 3;}
inline uint16_t kind(const CardState&){return 4;}
inline uint16_t kind(const PushHistory&){return 5;}
inline uint16_t kind(const FactoryState&){return 6;}
inline uint16_t kind(const ZoneState&){return 7;}
inline uint16_t kind(const RhythmState&){return 8;}
inline uint16_t kind(const RunState&){return 9;}
inline uint16_t kind(const MachineState&){return 10;}
// The revision must change whenever immutable definitions change. Engine NativeModule
// VERSION/config must also match; this envelope does not bypass engine save validation.
template<class State> std::string save(State state,uint64_t revision=1) {
    if(!revision||!valid(state))return {};
    std::string bytes;Archive a(&bytes);uint32_t magic=0x53545241;uint16_t version=1,type=kind(state);
    a(magic,version,type,revision,state);if(bytes.size()>2*1024*1024)return {};return bytes;
}
template<class State> bool load(const std::string& bytes,State& state,uint64_t expectedRevision=1) {
    if(!expectedRevision||bytes.size()>2*1024*1024)return false;
    Archive a(bytes);uint32_t magic=0;uint16_t version=0,type=0;uint64_t revision=0;State copy;
    a(magic,version,type,revision);if(!a.ok()||magic!=0x53545241||version!=1||type!=kind(copy)||revision!=expectedRevision)return false;
    a(copy);if(!a.ok()||a.remaining()||!valid(copy)||save(copy,revision)!=bytes)return false;
    state=std::move(copy);return true;
}
using runtime::storeBytes;
using runtime::loadBytes;
template<class NativeState,class State> bool storeState(NativeState& state,const std::string& key,const State& value,uint64_t revision=1){return !key.empty()&&storeBytes(state,key,save(value,revision));}
template<class NativeState,class State> bool restoreState(const NativeState& state,const std::string& key,State& value,uint64_t revision=1){auto bytes=loadBytes(state,key);return bytes&&load(*bytes,value,revision);}
}
