#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for boss-encounters state.
// Record kinds 112..119 are reserved for this package (112..117 used). Store each state under its own NativeState
// key and bind encounterDataHash (EncounterData.h) in the save envelope: definitions are not saved.
#include "BossEncounter.h"
#include "BossSchedule.h"
#include "MobBehaviour.h"
#include "StatusEffects.h"
#include "Telegraphs.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,TelegraphInstance& s){a(s.id,s.attack,s.source,s.team,s.target,s.started,s.lastTick,s.lastStrike,s.pose.origin.x,s.pose.origin.y,s.pose.yaw,s.caster.x,s.caster.y,s.live,s.stepped,s.released,s.struck,s.hit);}
inline void io(Archive& a,StatusInstance& s){a(s.serial,s.def,s.source,s.applied,s.expires,s.nextTick);}
inline void io(Archive& a,StatusActorState& s){a(s.active,s.immuneKinds,s.immuneDefs,s.nextSerial,s.lastTick,s.initialized);}
inline void io(Archive& a,BossState& s){a(s.boss,s.phase,s.started,s.phaseStarted,s.readyAt,s.lastTick,s.cursor,s.attackReady,s.uses,s.addNext,s.pendingAttack,s.pendingKey,s.busy,s.enraged,s.defeated,s.initialized);}
inline void io(Archive& a,BossRotation& s){a(s.seenMask,s.last);}
inline void io(Archive& a,EncounterRequest& r){a(r.window,r.participant,r.boss,r.entity,r.spawnedAt,r.breatherUntil,r.spawned,r.defeated,r.cancelled);}
inline void io(Archive& a,ScheduleState& s){a(s.next,s.lastTick,s.selected,s.requests,s.rotation,s.initialized);}
inline void io(Archive& a,MobState& s){a(s.mode,s.target,s.goal.x,s.goal.y,s.legFrom.x,s.legFrom.y,s.until,s.lastTick,s.initialized);}
inline bool valid(const TelegraphInstance& s){
    return s.id>0&&s.attack>0&&s.source>0&&s.team>=0&&s.target>=0&&s.started<=s.lastTick&&valid(s.pose)&&areaBounded(s.caster)&&s.hit.size()<=kTelegraphMaxTargets
        &&std::is_sorted(s.hit.begin(),s.hit.end())&&std::adjacent_find(s.hit.begin(),s.hit.end())==s.hit.end()&&(s.hit.empty()||s.hit.front()>0)&&(!s.struck||s.released)&&(!s.released||s.stepped);
}
inline bool valid(const StatusActorState& s){
    if(s.active.size()>kStatusMaxInstances||(s.immuneKinds>>4)||!std::is_sorted(s.immuneDefs.begin(),s.immuneDefs.end())||std::adjacent_find(s.immuneDefs.begin(),s.immuneDefs.end())!=s.immuneDefs.end())return false;
    uint64_t previous=0;for(const auto& x:s.active){if(x.serial<=previous||x.serial>=s.nextSerial||x.def<=0||x.source<0||x.applied>x.expires||x.nextTick<x.applied)return false;previous=x.serial;}
    return s.nextSerial>=1&&(s.initialized||(s.lastTick==0));
}
inline bool valid(const BossState& s){return s.boss>0&&s.phase<kBossMaxPhases&&s.started<=s.phaseStarted&&s.addNext.size()<=kBossMaxAdds&&s.pendingAttack>=0&&s.pendingKey>=0&&(s.initialized||(s.lastTick==0&&!s.defeated));}
inline bool valid(const BossRotation& r){return r.last<=8;}
inline bool valid(const ScheduleState& s){
    if(s.selected.size()!=s.next||s.next>kScheduleMaxWindows||s.requests.size()>kScheduleMaxWindows*kScheduleMaxParticipants||!valid(s.rotation))return false;
    for(const auto& r:s.requests)if(r.window<=0||r.participant<0||r.boss<=0||r.entity<0||(r.defeated&&!r.spawned))return false;
    return true;
}
inline bool valid(const MobState& s){return uint8_t(s.mode)<=uint8_t(MobMode::Return)&&s.target>=0&&areaBounded(s.goal)&&areaBounded(s.legFrom);}
inline uint16_t stateKind(const TelegraphInstance&){return 112;}
inline uint16_t stateKind(const StatusActorState&){return 113;}
inline uint16_t stateKind(const BossState&){return 114;}
inline uint16_t stateKind(const ScheduleState&){return 115;}
inline uint16_t stateKind(const MobState&){return 116;}
inline uint16_t stateKind(const BossRotation&){return 117;}
// Canonical record bytes folded into the simulation checksum (RtsModule / NativeState hashing).
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
void hashEncounterState(Hasher& h,const State& s){std::string bytes;Archive a(&bytes);auto copy=s;uint16_t kind=stateKind(copy);a(kind,copy);h.str(bytes);}
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
uint64_t encounterHash(const State& s){Hasher h;hashEncounterState(h,s);return h.value();}
}
