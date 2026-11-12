#pragma once
// Boss encounter schedule (boss-encounters): timed windows that pick one boss variant per window (weighted,
// optional "not the previous one" or an archetype rotation that shows every archetype once before repeating and
// never the last one twice), personal requests (one per participant per window, as Drone Fall's personal bosses)
// or one shared request, at most one successful spawn per request, breathers after a win, and the "may this
// participant get ordinary waves now" gate. The owner places the boss and reports spawn / defeat / cancel.
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kScheduleMaxWindows=64,kScheduleMaxVariants=32,kScheduleMaxParticipants=64;
struct BossVariant {int64_t boss=0;uint32_t weight=1;uint8_t archetype=0;}; // archetype 0: outside the rotation
// A window opens at `start`; unspawned requests are dropped at `end` (no boss after the window). breather: ticks
// without ordinary waves after the participant's boss of this window died. pauseWaves: no waves while it lives.
struct EncounterWindowDef {int64_t id=0;uint32_t start=0,end=0,breather=0;std::vector<BossVariant> variants;bool excludePrevious=false,pauseWaves=true;};
struct ScheduleDef {std::vector<EncounterWindowDef> windows;bool personal=true,rotation=false;uint8_t archetypes=0;}; // archetypes: rotation size (1..8)
struct BossRotation {uint8_t seenMask=0,last=0;};   // persist across matches in the player's profile
struct EncounterRequest {int64_t window=0,participant=0,boss=0,entity=0;uint32_t spawnedAt=0,breatherUntil=0;bool spawned=false,defeated=false,cancelled=false;};
struct ScheduleState {uint32_t next=0,lastTick=0;std::vector<int64_t> selected;std::vector<EncounterRequest> requests;BossRotation rotation;bool initialized=false;};
enum class ScheduleEventKind:uint8_t {WindowOpened,Pending,Expired};
struct ScheduleEvent {ScheduleEventKind kind=ScheduleEventKind::WindowOpened;int64_t window=0,participant=0,boss=0;};
inline bool valid(const ScheduleDef& d){
    if(d.windows.size()>kScheduleMaxWindows||(d.rotation&&(d.archetypes==0||d.archetypes>8)))return false;
    uint32_t previous=0;
    for(std::size_t i=0;i<d.windows.size();++i){
        const auto& w=d.windows[i];
        if(w.id<=0||w.end<=w.start||w.start<previous||w.breather>3600000||w.variants.empty()||w.variants.size()>kScheduleMaxVariants)return false;
        for(std::size_t j=0;j<i;++j)if(d.windows[j].id==w.id)return false;
        for(const auto& v:w.variants)if(v.boss<=0||v.weight==0||v.weight>1000000||v.archetype>8||(d.rotation&&v.archetype>d.archetypes))return false;
        previous=w.start;
    }
    return true;
}
inline uint8_t rotationFull(const ScheduleDef& d){return uint8_t((1u<<d.archetypes)-1u);}
inline bool rotationAllows(const ScheduleDef& d,const BossRotation& r,uint8_t archetype){
    if(!d.rotation||archetype==0)return true;
    auto bit=uint8_t(1u<<(archetype-1));
    return (r.seenMask&rotationFull(d))==rotationFull(d)?archetype!=r.last:(r.seenMask&bit)==0;
}
inline void rotationRecord(const ScheduleDef& d,BossRotation& r,uint8_t archetype){
    if(!d.rotation||archetype==0)return;
    if((r.seenMask&rotationFull(d))==rotationFull(d))r.seenMask=0;
    r.seenMask|=uint8_t(1u<<(archetype-1));r.last=archetype;
}
// Weighted pick among allowed variants; 0 when none is allowed (the window then has no boss). One rng draw at most.
inline int64_t selectBossVariant(const ScheduleDef& d,const EncounterWindowDef& w,BossRotation& r,int64_t previous,Random& rng){
    auto allowed=[&](const BossVariant& v){return rotationAllows(d,r,v.archetype)&&!(!d.rotation&&w.excludePrevious&&v.boss==previous&&w.variants.size()>1);};
    uint64_t total=0;for(const auto& v:w.variants)if(allowed(v))total+=v.weight;if(!total)return 0;
    uint64_t roll=total>1?rng.below(uint32_t(total)):0;
    for(const auto& v:w.variants){if(!allowed(v))continue;if(roll<v.weight){rotationRecord(d,r,v.archetype);return v.boss;}roll-=v.weight;}
    return 0;
}
namespace detail {
inline bool participantsValid(std::span<const int64_t> p){
    if(p.size()>kScheduleMaxParticipants)return false;
    for(std::size_t i=0;i<p.size();++i)if(p[i]<=0||(i&&p[i]<=p[i-1]))return false;
    return true;
}
}
// Once per tick. participants: living, still playing ids in ascending order (personal: each gets a request when a
// window opens; shared: one request with participant 0). Returns WindowOpened (boss 0: none allowed), then one
// Pending per unspawned request (keep trying to place it), then Expired for requests dropped at window end.
inline std::optional<std::vector<ScheduleEvent>> stepSchedule(const ScheduleDef& d,ScheduleState& state,uint32_t tick,std::span<const int64_t> participants,Random& rng){
    if(!valid(d)||!detail::participantsValid(participants)||(state.initialized&&tick<=state.lastTick)||state.next>d.windows.size()||state.selected.size()!=state.next)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;std::vector<ScheduleEvent> out;
    while(s.next<d.windows.size()&&tick>=d.windows[s.next].start){
        const auto& w=d.windows[s.next];int64_t previous=s.selected.empty()?0:s.selected.back();
        auto boss=tick<w.end?selectBossVariant(d,w,s.rotation,previous,rng):0;s.selected.push_back(boss);++s.next;
        out.push_back({ScheduleEventKind::WindowOpened,w.id,0,boss});
        if(!boss)continue;
        if(d.personal)for(auto p:participants)s.requests.push_back({w.id,p,boss});
        else s.requests.push_back({w.id,0,boss});
    }
    for(auto& r:s.requests){
        if(r.spawned||r.cancelled)continue;
        const EncounterWindowDef* w=nullptr;for(const auto& x:d.windows)if(x.id==r.window)w=&x;
        if(!w||tick>=w->end){r.cancelled=true;out.push_back({ScheduleEventKind::Expired,r.window,r.participant,r.boss});continue;}
        if(r.participant&&!std::binary_search(participants.begin(),participants.end(),r.participant))continue;
        out.push_back({ScheduleEventKind::Pending,r.window,r.participant,r.boss});
    }
    state=std::move(s);return out;
}
inline EncounterRequest* findRequest(ScheduleState& s,int64_t window,int64_t participant){
    for(auto& r:s.requests)if(r.window==window&&r.participant==participant)return &r;
    return nullptr;
}
inline bool markBossSpawned(ScheduleState& s,int64_t window,int64_t participant,int64_t entity,uint32_t tick){
    auto r=findRequest(s,window,participant);if(!r||r->spawned||r->cancelled||entity<=0)return false;r->spawned=true;r->entity=entity;r->spawnedAt=tick;return true;
}
// A kill (not a removal): starts the breather. Cancel (participant left, boss removed) is never a win.
inline bool markBossDefeated(const ScheduleDef& d,ScheduleState& s,int64_t entity,uint32_t tick){
    for(auto& r:s.requests)if(r.spawned&&!r.defeated&&!r.cancelled&&r.entity==entity){
        r.defeated=true;uint32_t breather=0;for(const auto& w:d.windows)if(w.id==r.window)breather=w.breather;
        r.breatherUntil=uint32_t(std::min<uint64_t>(uint64_t(tick)+breather,std::numeric_limits<uint32_t>::max()));return true;
    }
    return false;
}
inline uint32_t cancelBossRequests(ScheduleState& s,int64_t participant){
    uint32_t n=0;for(auto& r:s.requests)if(r.participant==participant&&!r.defeated&&!r.cancelled){r.cancelled=true;++n;}return n;
}
// Ordinary waves for this participant (0 = everyone in shared mode): none while an own boss is pending in its window
// or alive (pauseWaves), and none during the breather after it died.
inline bool ordinaryWavesAllowed(const ScheduleDef& d,const ScheduleState& s,uint32_t tick,int64_t participant){
    for(const auto& r:s.requests){
        if(r.participant!=participant&&r.participant!=0)continue;
        if(r.cancelled)continue;
        bool pause=true;for(const auto& w:d.windows)if(w.id==r.window)pause=w.pauseWaves;
        if(r.defeated){if(tick<r.breatherUntil)return false;continue;}
        if(pause)return false;
    }
    return true;
}
inline uint32_t bossesDefeated(const ScheduleState& s,int64_t participant){uint32_t n=0;for(const auto& r:s.requests)if(r.participant==participant&&r.defeated)++n;return n;}
}
