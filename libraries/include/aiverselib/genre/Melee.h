#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace aiverse::lib::genre {
enum class AttackPhase {Idle,Windup,Active,Recovery};
struct AttackDef {int64_t id=0;uint32_t windup=6,active=4,recovery=12,cancelFrom=10,bufferTicks=8;std::vector<int64_t> combos;};
using AttackSet=std::map<int64_t,AttackDef>;
struct MeleeState {int64_t attack=0,queued=0;uint32_t started=0,queuedAt=0,lastTick=0;bool initialized=false;std::vector<int64_t> hitTargets;bool operator==(const MeleeState&)const=default;};
inline bool valid(const AttackSet& defs){
    if(defs.empty()||defs.size()>1024)return false;
    for(const auto& [id,d]:defs){if(id<=0||d.id!=id||d.active==0||d.windup>3600||d.active>3600||d.recovery>3600||d.cancelFrom>d.windup+d.active+d.recovery||d.bufferTicks>3600)return false;for(auto next:d.combos)if(!defs.contains(next))return false;}return true;
}
inline AttackPhase attackPhase(const AttackSet& defs,const MeleeState& s,uint32_t tick){
    auto it=defs.find(s.attack);if(it==defs.end()||tick<s.started)return AttackPhase::Idle;const auto& d=it->second;auto age=tick-s.started;
    if(age<d.windup)return AttackPhase::Windup;
    if(age<d.windup+d.active)return AttackPhase::Active;
    if(age<d.windup+d.active+d.recovery)return AttackPhase::Recovery;
    return AttackPhase::Idle;
}
// Authorization includes stamina/cooldown/ownership; this helper owns only custom animation/combo timing.
template<class Authorize>
bool stepMelee(const AttackSet& defs,MeleeState& state,uint32_t tick,int64_t requested,Authorize authorize){
    if(!valid(defs)||(state.attack&&!defs.contains(state.attack))||(state.queued&&!defs.contains(state.queued))||(state.initialized&&tick<=state.lastTick)||(requested&&!defs.contains(requested)))return false;
    auto s=state;s.lastTick=tick;s.initialized=true;
    auto phase=attackPhase(defs,s,tick);
    if(requested){s.queued=requested;s.queuedAt=tick;}
    auto active=defs.find(s.attack);bool canStart=phase==AttackPhase::Idle;
    if(s.queued&&tick-s.queuedAt>defs.at(s.queued).bufferTicks)s.queued=0;
    if(s.queued&&active!=defs.end()){
        if(tick-s.queuedAt>active->second.bufferTicks)s.queued=0;
        else if(tick-s.started>=active->second.cancelFrom&&std::find(active->second.combos.begin(),active->second.combos.end(),s.queued)!=active->second.combos.end())canStart=true;
    }
    if(s.queued&&canStart&&authorize(s.queued)){s.attack=s.queued;s.queued=0;s.started=tick;s.hitTargets.clear();}
    else if(phase==AttackPhase::Idle){s.attack=0;s.hitTargets.clear();}
    state=std::move(s);return true;
}
inline bool recordMeleeHit(const AttackSet& defs,MeleeState& s,uint32_t tick,int64_t target){
    if(target<=0||attackPhase(defs,s,tick)!=AttackPhase::Active||s.hitTargets.size()>=4096)return false;
    auto at=std::lower_bound(s.hitTargets.begin(),s.hitTargets.end(),target);if(at!=s.hitTargets.end()&&*at==target)return false;s.hitTargets.insert(at,target);return true;
}
enum class MeleeContact {Hit,Blocked,Parried,Evaded};
inline MeleeContact resolveMeleeContact(bool invulnerable,bool parryWindow,bool blocking,bool insideGuard){
    if(invulnerable)return MeleeContact::Evaded;
    if(insideGuard&&parryWindow)return MeleeContact::Parried;
    if(insideGuard&&blocking)return MeleeContact::Blocked;
    return MeleeContact::Hit;
}
}
