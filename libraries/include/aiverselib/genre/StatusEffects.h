#pragma once
// Custom status owner for native/host actors (boss-encounters): slow, root, stun and burn instances with
// Independent / Refresh / Reject stacking, immunity by kind or id, periodic damage on exact ticks and view
// events (one per instance, Ground or ModelTop anchor) for status visuals. Inside RtsModule games the engine's
// statuses stay authoritative: use rtsStatusRules to express the same definition as a rules.json status instead.
#include "engine/core/Fixed.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kStatusMaxInstances=256;
enum class StatusKind:uint8_t {Slow,Root,Stun,Burn};
enum class StatusStacking:uint8_t {Independent,Refresh,Reject};
enum class StatusAnchor:uint8_t {Ground,ModelTop};
// speedMilli: movement multiplier while a Slow is active (lowest wins). Burn deals damagePerTick every tickInterval
// ticks, the first one tickInterval after application, the last one on the expiry tick: ceil(duration / interval) hits.
struct StatusEffectDef {int64_t id=0;StatusKind kind=StatusKind::Slow;uint32_t duration=60,tickInterval=10,maxStacks=1;int64_t speedMilli=500,damagePerTick=0;
    StatusStacking stacking=StatusStacking::Refresh;StatusAnchor anchor=StatusAnchor::Ground;};
using StatusSet=std::map<int64_t,StatusEffectDef>;
inline uint32_t statusKindBit(StatusKind k){return 1u<<uint8_t(k);}
inline bool valid(const StatusEffectDef& d){
    return d.id>0&&uint8_t(d.kind)<=uint8_t(StatusKind::Burn)&&d.duration>0&&d.duration<=360000&&d.tickInterval>0&&d.tickInterval<=36000
        &&d.maxStacks>=1&&d.maxStacks<=64&&d.speedMilli>=0&&d.speedMilli<=1000&&d.damagePerTick>=0&&d.damagePerTick<=1000000000
        &&uint8_t(d.stacking)<=uint8_t(StatusStacking::Reject)&&uint8_t(d.anchor)<=uint8_t(StatusAnchor::ModelTop)&&(d.kind==StatusKind::Burn||d.damagePerTick==0);
}
inline bool valid(const StatusSet& defs){if(defs.size()>4096)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}
struct StatusInstance {uint64_t serial=0;int64_t def=0,source=0;uint32_t applied=0,expires=0,nextTick=0;};
struct StatusActorState {std::vector<StatusInstance> active;uint32_t immuneKinds=0;std::vector<int64_t> immuneDefs;uint64_t nextSerial=1;uint32_t lastTick=0;bool initialized=false;};
enum class StatusApply:uint8_t {Applied,Refreshed,Rejected,Immune,Invalid};
enum class StatusViewKind:uint8_t {Started,Refreshed,Ended};
struct StatusViewEvent {StatusViewKind kind=StatusViewKind::Started;uint64_t serial=0;int64_t def=0;StatusKind status=StatusKind::Slow;StatusAnchor anchor=StatusAnchor::Ground;uint32_t expires=0;};
struct StatusApplyResult {StatusApply result=StatusApply::Invalid;uint64_t serial=0;std::optional<StatusViewEvent> view;};
struct StatusDamage {uint64_t serial=0;int64_t def=0,source=0,amount=0;};
// Aggregate for this tick: movement multiplier (0 when stunned or rooted), whether the actor may attack/cast.
struct StatusTick {int64_t speedMilli=1000;bool stunned=false,rooted=false;std::vector<StatusDamage> damage;std::vector<StatusViewEvent> events;};
inline bool statusImmune(const StatusActorState& s,const StatusEffectDef& d){return (s.immuneKinds&statusKindBit(d.kind))||std::binary_search(s.immuneDefs.begin(),s.immuneDefs.end(),d.id);}
// Immunity (e.g. bosses ignore stun but burn): kinds as a statusKindBit mask, ids sorted unique.
inline bool setStatusImmunity(StatusActorState& s,uint32_t kinds,std::vector<int64_t> ids){
    if(kinds>>4)return false;
    std::sort(ids.begin(),ids.end());
    if(std::adjacent_find(ids.begin(),ids.end())!=ids.end()||(!ids.empty()&&ids.front()<=0))return false;
    s.immuneKinds=kinds;s.immuneDefs=std::move(ids);return true;
}
// Apply on the strike tick (before or after stepStatuses of the same tick; the first effect lands one interval later).
inline StatusApplyResult applyStatus(const StatusSet& defs,StatusActorState& s,uint32_t tick,int64_t def,int64_t source){
    StatusApplyResult r;auto it=defs.find(def);
    if(it==defs.end()||!valid(it->second)||source<0||(s.initialized&&tick<s.lastTick)||uint64_t(tick)+it->second.duration>std::numeric_limits<uint32_t>::max())return r;
    const auto& d=it->second;
    if(statusImmune(s,d)){r.result=StatusApply::Immune;return r;}
    std::size_t same=0;StatusInstance* existing=nullptr;
    for(auto& x:s.active)if(x.def==def){++same;if(!existing)existing=&x;}
    if(existing&&d.stacking==StatusStacking::Reject){r.result=StatusApply::Rejected;return r;}
    if(existing&&d.stacking==StatusStacking::Refresh){
        existing->expires=std::max(existing->expires,tick+d.duration);existing->source=source;
        r.result=StatusApply::Refreshed;r.serial=existing->serial;r.view=StatusViewEvent{StatusViewKind::Refreshed,existing->serial,d.id,d.kind,d.anchor,existing->expires};return r;
    }
    if(same>=d.maxStacks||s.active.size()>=kStatusMaxInstances){r.result=StatusApply::Rejected;return r;}
    StatusInstance x;x.serial=s.nextSerial++;x.def=def;x.source=source;x.applied=tick;x.expires=tick+d.duration;x.nextTick=uint32_t(std::min<uint64_t>(uint64_t(tick)+d.tickInterval,x.expires));
    s.active.push_back(x);r.result=StatusApply::Applied;r.serial=x.serial;r.view=StatusViewEvent{StatusViewKind::Started,x.serial,d.id,d.kind,d.anchor,x.expires};return r;
}
// Removes every instance (death, cleanse). Ended view events in serial order.
inline std::vector<StatusViewEvent> clearStatuses(const StatusSet& defs,StatusActorState& s){
    std::vector<StatusViewEvent> out;for(const auto& x:s.active){auto it=defs.find(x.def);if(it!=defs.end())out.push_back({StatusViewKind::Ended,x.serial,x.def,it->second.kind,it->second.anchor,x.expires});}
    s.active.clear();return out;
}
// Once per tick: periodic damage (instances in serial order, catching up skipped ticks), then expiry, then aggregates.
inline std::optional<StatusTick> stepStatuses(const StatusSet& defs,StatusActorState& state,uint32_t tick){
    if(!valid(defs)||(state.initialized&&tick<=state.lastTick))return std::nullopt;
    for(const auto& x:state.active)if(!defs.contains(x.def))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;StatusTick out;
    std::vector<StatusInstance> keep;keep.reserve(s.active.size());
    for(auto& x:s.active){
        const auto& d=defs.at(x.def);
        if(d.kind==StatusKind::Burn)while(x.nextTick<=tick&&x.nextTick<=x.expires&&x.nextTick>x.applied){
            out.damage.push_back({x.serial,x.def,x.source,d.damagePerTick});
            if(x.nextTick==x.expires){x.nextTick=x.expires+1;break;}
            x.nextTick=uint32_t(std::min<uint64_t>(uint64_t(x.nextTick)+d.tickInterval,x.expires));
        }
        if(x.expires<=tick){out.events.push_back({StatusViewKind::Ended,x.serial,x.def,d.kind,d.anchor,x.expires});continue;}
        if(d.kind==StatusKind::Stun)out.stunned=true;
        else if(d.kind==StatusKind::Root)out.rooted=true;
        else if(d.kind==StatusKind::Slow)out.speedMilli=std::min(out.speedMilli,d.speedMilli);
        keep.push_back(x);
    }
    if(out.stunned||out.rooted)out.speedMilli=0;
    s.active=std::move(keep);state=std::move(s);return out;
}
// The same definition for games running on the RTS simulation: `status` is a rules.json statuses entry (slow ->
// speed mod, root/stun -> flags, burn -> tick damage; independent or refresh stacking; Reject has no engine
// equivalent and maps to refresh with one stack) and `duration` the seconds for the applying status effect.
struct RtsStatusRules {std::string status,duration;};
inline RtsStatusRules rtsStatusRules(const StatusEffectDef& d,uint32_t ticksPerSecond){
    if(!valid(d)||ticksPerSecond==0)return {};
    auto seconds=[&](uint32_t t){return Fixed::fromRatio(t,ticksPerSecond).toString();};
    std::string out="{\"tags\": [\"debuff\"]";
    if(d.kind==StatusKind::Slow)out+=", \"mods\": [{\"stat\": \"speed\", \"mul\": "+Fixed::fromRatio(d.speedMilli,1000).toString()+"}]";
    else if(d.kind==StatusKind::Root)out+=", \"flags\": [\"root\"]";
    else if(d.kind==StatusKind::Stun)out+=", \"flags\": [\"stun\"]";
    else out+=", \"tick\": {\"damage\": "+std::to_string(d.damagePerTick)+", \"interval\": "+seconds(d.tickInterval)+"}";
    out+=d.stacking==StatusStacking::Independent?", \"stacking\": \"independent\", \"maxStacks\": "+std::to_string(d.maxStacks):std::string(", \"stacking\": \"refresh\"");
    return {out+"}",seconds(d.duration)};
}
}
