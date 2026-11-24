#pragma once
#include "engine/mechanics/Namespace.h"
// Phased safe zone ("the circle"): a sequence of phases, each one announces the next circle, waits, then
// shrinks the current circle into it linearly. The next centre is seeded inside the current circle (so the
// new circle is fully inside the old one unless the phase pins an authored centre), scaled by `centerPull`
// (0 = concentric, 1 = anywhere that still fits) and clamped to the playable bounds. Outside the circle the
// owner applies the phase's damage every `damageInterval` ticks. Not tied to any genre: an RTS "shrinking
// map", a top-down arena or an FPS battle royale all use the same stepper.
// stepSafeZone is called once per strictly increasing tick, returns the phase events of that tick, and returns
// std::nullopt without touching the state for invalid definitions, states or repeated / backward ticks.
#include "engine/mechanics/royale/Common.h"
namespace aiverse::lib::mechanics {

struct SafeZonePhase{
    uint32_t waitTicks=0,shrinkTicks=1;   // wait after the announcement, then shrink; shrinkTicks 0 = jump
    Fixed radius;                         // target radius of this phase
    int32_t damage=1;uint32_t damageInterval=30;   // per hit while outside during this phase (0 = no damage)
    std::optional<Vec2> center;           // authored target centre (skips seeding)
    bool operator==(const SafeZonePhase&)const=default;
};
struct SafeZoneDef{
    RoyaleBounds bounds;RoyaleCircle initial;uint32_t startTick=0;
    Fixed centerPull=1_fx;std::vector<SafeZonePhase> phases;
    int32_t preDamage=0;uint32_t preDamageInterval=0;   // damage before the first phase starts (usually 0)
};
enum class SafeZoneStage:uint8_t{Idle,Waiting,Shrinking,Closed};
struct SafeZoneState{
    SafeZoneStage stage=SafeZoneStage::Idle;uint32_t phase=0,stageStart=0,lastTick=0;
    RoyaleCircle from,to;RoyaleRng rng;bool initialized=false;
    bool operator==(const SafeZoneState&)const=default;
};
enum class SafeZoneEventKind:uint8_t{Announced,ShrinkStarted,ShrinkEnded,Closed};
struct SafeZoneEvent{SafeZoneEventKind kind{};uint32_t phase=0;RoyaleCircle circle;bool operator==(const SafeZoneEvent&)const=default;};
using SafeZoneEvents=std::vector<SafeZoneEvent>;

inline bool valid(const SafeZonePhase& p){
    using namespace royaleDetail;
    return p.waitTicks<=kRoyaleMaxTicks&&p.shrinkTicks<=kRoyaleMaxTicks&&radiusOk(p.radius)&&p.damage>=0&&p.damageInterval<=kRoyaleMaxTicks&&(!p.damage||p.damageInterval)&&(!p.center||pointOk(*p.center));
}
inline bool valid(const SafeZoneDef& d){
    if(!valid(d.bounds)||!valid(d.initial)||d.initial.radius<=Fixed{}||d.startTick>kRoyaleMaxTicks||d.centerPull<Fixed{}||d.centerPull>1_fx||d.phases.empty()||d.phases.size()>kRoyaleMaxPhases)return false;
    if(d.preDamage<0||(d.preDamage&&!d.preDamageInterval))return false;
    Fixed last=d.initial.radius;for(const auto& p:d.phases){if(!valid(p)||p.radius>last)return false;last=p.radius;}
    return true;
}
inline bool valid(const SafeZoneState& s){
    return uint8_t(s.stage)<=uint8_t(SafeZoneStage::Closed)&&s.phase<kRoyaleMaxPhases&&valid(s.from)&&valid(s.to)&&s.stageStart<=kRoyaleMaxTicks&&(s.rng.inc&1)&&(s.initialized||(s.stage==SafeZoneStage::Idle&&!s.lastTick));
}

namespace royaleDetail {
inline RoyaleCircle nextCircle(const SafeZoneDef& d,const SafeZonePhase& p,const RoyaleCircle& current,Random& g){
    if(p.center)return {clampTo(*p.center,d.bounds),p.radius};
    auto free=(current.radius-p.radius)*d.centerPull;auto offset=unitDisc(g)*free;
    return {clampTo(current.center+offset,d.bounds),p.radius};
}
}
// Fresh zone state seeded for this match (a default SafeZoneState has no stream and is rejected). Phase 0 is
// announced by the first stepSafeZone at or after def.startTick.
inline SafeZoneState beginSafeZone(uint64_t seed){SafeZoneState s;s.rng=royaleDetail::seeded(seed,0x5a0e);return s;}

inline std::optional<SafeZoneEvents> stepSafeZone(const SafeZoneDef& d,SafeZoneState& state,uint32_t tick){
    using namespace royaleDetail;using K=SafeZoneEventKind;
    if(!valid(d)||!valid(state)||!nextTick(state.initialized,state.lastTick,tick)||state.phase>=d.phases.size())return {};
    auto s=state;SafeZoneEvents events;
    if(!s.initialized){s.initialized=true;s.from=s.to=d.initial;}
    s.lastTick=tick;
    auto g=rng(s.rng);
    auto announce=[&](uint32_t at){
        const auto& p=d.phases[s.phase];s.to=nextCircle(d,p,s.from,g);s.stage=SafeZoneStage::Waiting;s.stageStart=at;
        events.push_back({K::Announced,s.phase,s.to});
    };
    if(s.stage==SafeZoneStage::Idle&&tick>=d.startTick)announce(d.startTick);
    // A tick may cross several stage boundaries (zero-length waits / shrinks or a coarse caller).
    for(int guard=0;guard<int(kRoyaleMaxPhases)*3&&s.stage!=SafeZoneStage::Idle&&s.stage!=SafeZoneStage::Closed;++guard){
        const auto& p=d.phases[s.phase];
        if(s.stage==SafeZoneStage::Waiting){
            if(tick<s.stageStart+p.waitTicks)break;
            s.stage=SafeZoneStage::Shrinking;s.stageStart+=p.waitTicks;events.push_back({K::ShrinkStarted,s.phase,s.to});
        }else{
            if(tick<s.stageStart+p.shrinkTicks)break;
            auto end=s.stageStart+p.shrinkTicks;s.from=s.to;events.push_back({K::ShrinkEnded,s.phase,s.to});
            if(s.phase+1>=d.phases.size()){s.stage=SafeZoneStage::Closed;s.stageStart=end;events.push_back({K::Closed,s.phase,s.to});}
            else{++s.phase;announce(end);}
        }
    }
    s.rng=store(g);state=s;return events;
}
// The safe circle at `tick` (interpolated while shrinking). Valid for any tick >= the state's last step.
inline RoyaleCircle safeZoneCircle(const SafeZoneDef& d,const SafeZoneState& s,uint32_t tick){
    if(s.stage!=SafeZoneStage::Shrinking||s.phase>=d.phases.size())return s.stage==SafeZoneStage::Idle&&!s.initialized?d.initial:s.from;
    auto len=d.phases[s.phase].shrinkTicks;if(!len||tick>=s.stageStart+len)return s.to;
    if(tick<=s.stageStart)return s.from;
    auto t=Fixed::fromRatio(int64_t(tick-s.stageStart),int64_t(len));
    return {royaleDetail::lerp(s.from.center,s.to.center,t),royaleDetail::lerp(s.from.radius,s.to.radius,t)};
}
// The announced target circle (equals safeZoneCircle once closed or before the start).
inline RoyaleCircle safeZoneTarget(const SafeZoneDef& d,const SafeZoneState& s){return s.stage==SafeZoneStage::Idle?d.initial:s.to;}
// Ticks until the current stage ends (0 when idle-before-start is unknown or closed): HUD countdowns.
inline uint32_t safeZoneTicksLeft(const SafeZoneDef& d,const SafeZoneState& s,uint32_t tick){
    if(s.phase>=d.phases.size())return 0;
    if(s.stage==SafeZoneStage::Idle)return tick<d.startTick?d.startTick-tick:0;
    if(s.stage==SafeZoneStage::Closed)return 0;
    const auto& p=d.phases[s.phase];auto end=s.stageStart+(s.stage==SafeZoneStage::Waiting?p.waitTicks:p.shrinkTicks);
    return end>tick?end-tick:0;
}
// Damage the owner applies this tick to an actor outside safeZoneCircle (0 = none this tick).
inline int32_t safeZoneDamage(const SafeZoneDef& d,const SafeZoneState& s,uint32_t tick){
    if(s.stage==SafeZoneStage::Idle)return d.preDamage&&d.preDamageInterval&&tick%d.preDamageInterval==0?d.preDamage:0;
    if(s.phase>=d.phases.size())return 0;
    const auto& p=d.phases[s.phase];
    if(!p.damage||!p.damageInterval||tick<d.startTick)return 0;
    return (tick-d.startTick)%p.damageInterval==0?p.damage:0;
}
inline bool outsideSafeZone(const SafeZoneDef& d,const SafeZoneState& s,uint32_t tick,Vec3 p){return !insideCircle(safeZoneCircle(d,s,tick),p);}
}
