#pragma once
// Boss brain (boss-encounters): ordered phases entered by HP threshold or encounter time, an attack pattern per
// phase (range bands like Drone Fall's cone <= 3 m / alternating fist slams <= 7 m / rock <= 15 m, a fixed
// sequence or a weighted draw), per-attack and shared cooldowns that start when the running attack ends, phase
// transition pauses, enrage (timer or HP) multipliers and add waves. Decisions only: the owner begins the chosen
// telegraph (Telegraphs.h), moves the boss, spawns adds and applies damage.
#include "Telegraphs.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kBossMaxPhases=16,kBossMaxPattern=32,kBossMaxAdds=8;
enum class PatternMode:uint8_t {ByRange,Sequence,Weighted};
// Range band [minRange, maxRange] on the XZ distance to the combat target (maxRange 0: any distance). `alternate`:
// every second use of this entry starts that attack instead (left / right fist). weight is used by Weighted.
struct BossPatternEntry {int64_t attack=0,alternate=0;Fixed minRange{},maxRange{};uint32_t weight=1;};
// every 0: one wave when the phase starts; otherwise a wave every `every` ticks while the phase lasts. maxAlive caps
// the wave by the adds of this entry still alive (0: no cap). Radii are the spawn ring around the boss.
struct AddWaveDef {int64_t unit=0;uint32_t count=1,maxAlive=0,every=0;Fixed minRadius=2_fx,maxRadius=6_fx;};
// Phase 0 starts the encounter. Phase i > 0 starts when hp <= hpBelowMilli/1000 of max, or (afterTicks > 0) when
// the encounter is that old, whichever is first. Multipliers are milli (1000 = unchanged) and stack with enrage.
struct BossPhaseDef {int64_t id=0,hpBelowMilli=1000,damageMilli=1000,speedMilli=1000,cooldownMilli=1000;uint32_t afterTicks=0,globalCooldown=0,transitionTicks=0;
    PatternMode mode=PatternMode::ByRange;std::vector<BossPatternEntry> pattern;std::vector<AddWaveDef> adds;};
struct EnrageDef {uint32_t afterTicks=0;int64_t hpBelowMilli=0,damageMilli=1000,speedMilli=1000,cooldownMilli=1000;}; // both 0: never
struct BossDef {int64_t id=0;uint8_t archetype=0;std::vector<BossPhaseDef> phases;EnrageDef enrage;};
using BossSet=std::map<int64_t,BossDef>;
namespace detail {
inline bool milli(int64_t v,int64_t high=100000){return v>=0&&v<=high;}
}
inline bool valid(const BossPhaseDef& p,const TelegraphSet& attacks,bool first){
    if(p.id<=0||!detail::milli(p.hpBelowMilli,1000)||!detail::milli(p.damageMilli)||!detail::milli(p.speedMilli)||!detail::milli(p.cooldownMilli)
        ||p.afterTicks>3600000||p.globalCooldown>36000||p.transitionTicks>36000||uint8_t(p.mode)>uint8_t(PatternMode::Weighted)
        ||p.pattern.size()>kBossMaxPattern||p.adds.size()>kBossMaxAdds||(!first&&p.hpBelowMilli>=1000&&p.afterTicks==0))return false;
    for(const auto& e:p.pattern){
        if(!attacks.contains(e.attack)||(e.alternate&&!attacks.contains(e.alternate))||e.weight>1000000||(p.mode==PatternMode::Weighted&&e.weight==0)
            ||e.minRange<Fixed{}||e.maxRange<Fixed{}||e.maxRange>kCombatCoordLimit||(e.maxRange>Fixed{}&&e.minRange>e.maxRange))return false;
    }
    for(const auto& a:p.adds)if(a.unit<=0||a.count==0||a.count>256||a.maxAlive>4096||a.every>3600000||a.minRadius<Fixed{}||a.maxRadius<a.minRadius||a.maxRadius>kCombatSizeLimit)return false;
    return true;
}
inline bool valid(const BossDef& b,const TelegraphSet& attacks){
    if(b.id<=0||b.phases.empty()||b.phases.size()>kBossMaxPhases||!valid(attacks))return false;
    for(std::size_t i=0;i<b.phases.size();++i){if(!valid(b.phases[i],attacks,i==0))return false;for(std::size_t j=0;j<i;++j)if(b.phases[j].id==b.phases[i].id)return false;}
    const auto& e=b.enrage;return e.afterTicks<=3600000&&detail::milli(e.hpBelowMilli,1000)&&detail::milli(e.damageMilli)&&detail::milli(e.speedMilli)&&detail::milli(e.cooldownMilli);
}
inline bool valid(const BossSet& bosses,const TelegraphSet& attacks){
    if(bosses.size()>1024)return false;
    for(const auto& [id,b]:bosses)if(id!=b.id||!valid(b,attacks))return false;
    return true;
}
struct BossState {
    int64_t boss=0;uint32_t phase=0,started=0,phaseStarted=0,readyAt=0,lastTick=0,cursor=0;
    std::map<int64_t,uint32_t> attackReady;   // attack id -> tick it may start again
    std::map<int64_t,uint32_t> uses;          // pattern attack id -> uses (alternation)
    std::vector<uint32_t> addNext;            // per add wave of the current phase: next wave tick (UINT32_MAX: done)
    int64_t pendingAttack=0,pendingKey=0;     // chosen attack + its pattern entry; cooldowns start when it finished
    bool busy=false,enraged=false,defeated=false,initialized=false;
};
// hp/maxHp from the health owner; targetDistance: XZ distance to the boss's combat target (nullopt: none);
// busy: an own telegraph or stagger is still running; aliveAdds[i]: living adds of wave i of the current phase.
struct BossInput {int64_t hp=1,maxHp=1;std::optional<Fixed> targetDistance;bool busy=false;std::span<const uint32_t> aliveAdds;};
enum class BossEventKind:uint8_t {Engaged,PhaseEntered,Enraged,Attack,SpawnAdds,Defeated};
struct BossEvent {BossEventKind kind=BossEventKind::Engaged;int64_t value=0,unit=0;uint32_t count=0;Fixed minRadius{},maxRadius{};};
struct BossStep {std::vector<BossEvent> events;int64_t attack=0,damageMilli=1000,speedMilli=1000,cooldownMilli=1000;uint32_t phase=0;bool transitioning=false;};
namespace detail {
inline int64_t mulMilli(int64_t a,int64_t b){return a*b/1000;}
inline bool phaseReached(const BossPhaseDef& p,int64_t hp,int64_t maxHp,uint32_t age){
    return (p.hpBelowMilli<1000&&hp*1000<=p.hpBelowMilli*maxHp)||(p.afterTicks>0&&age>=p.afterTicks);
}
inline uint32_t scaled(uint32_t ticks,int64_t milli){return uint32_t(std::min<int64_t>(int64_t(ticks)*milli/1000,36000));}
inline uint32_t after(uint32_t tick,uint32_t ticks){return uint32_t(std::min<uint64_t>(uint64_t(tick)+ticks,std::numeric_limits<uint32_t>::max()));}
}
inline const BossPhaseDef* bossPhase(const BossSet& bosses,const BossState& s){
    auto it=bosses.find(s.boss);if(it==bosses.end()||s.phase>=it->second.phases.size())return nullptr;return &it->second.phases[s.phase];
}
// Once per boss per tick. RNG (sim.rng() or one saved with the sim) is drawn only by Weighted phases with two or
// more eligible entries. Invalid input or a repeated/backwards tick returns nullopt with the state unchanged.
inline std::optional<BossStep> stepBoss(const BossSet& bosses,const TelegraphSet& attacks,BossState& state,uint32_t tick,const BossInput& in,Random& rng){
    auto it=bosses.find(state.boss);
    if(it==bosses.end()||!valid(it->second,attacks)||in.maxHp<=0||in.hp>in.maxHp||in.maxHp>1000000000000ll||(state.initialized&&tick<=state.lastTick)
        ||(in.targetDistance&&(*in.targetDistance<Fixed{}||*in.targetDistance>kCombatCoordLimit))||state.phase>=it->second.phases.size())return std::nullopt;
    const auto& b=it->second;auto s=state;BossStep out;auto hp=std::max<int64_t>(in.hp,0);
    if(s.defeated){s.lastTick=tick;out.phase=s.phase;state=std::move(s);return out;}
    auto enterPhase=[&](uint32_t index){
        s.phase=index;s.phaseStarted=tick;s.cursor=0;const auto& p=b.phases[index];
        s.addNext.assign(p.adds.size(),tick);
        if(index>0)s.readyAt=std::max(s.readyAt,detail::after(tick,p.transitionTicks));
        out.events.push_back({BossEventKind::PhaseEntered,p.id});
    };
    if(!s.initialized){s.initialized=true;s.started=tick;s.readyAt=tick;out.events.push_back({BossEventKind::Engaged,b.id});enterPhase(0);}
    s.lastTick=tick;
    if(hp==0){s.defeated=true;s.pendingAttack=0;s.pendingKey=0;out.events.push_back({BossEventKind::Defeated,b.id});out.phase=s.phase;state=std::move(s);return out;}
    auto age=tick-s.started;
    while(s.phase+1<b.phases.size()&&detail::phaseReached(b.phases[s.phase+1],hp,in.maxHp,age))enterPhase(s.phase+1);
    const auto& e=b.enrage;
    if(!s.enraged&&((e.afterTicks>0&&age>=e.afterTicks)||(e.hpBelowMilli>0&&hp*1000<=e.hpBelowMilli*in.maxHp))){s.enraged=true;out.events.push_back({BossEventKind::Enraged,b.id});}
    const auto& p=b.phases[s.phase];
    out.phase=s.phase;out.damageMilli=p.damageMilli;out.speedMilli=p.speedMilli;out.cooldownMilli=p.cooldownMilli;
    if(s.enraged){out.damageMilli=detail::mulMilli(out.damageMilli,e.damageMilli);out.speedMilli=detail::mulMilli(out.speedMilli,e.speedMilli);out.cooldownMilli=detail::mulMilli(out.cooldownMilli,e.cooldownMilli);}
    // Cooldowns of the finished attack start now, after its whole windup/active/recovery interval.
    if(s.busy&&!in.busy){
        if(s.pendingAttack){auto cd=attacks.at(s.pendingAttack).cooldown;s.attackReady[s.pendingKey]=detail::after(tick,detail::scaled(cd,out.cooldownMilli));}
        s.readyAt=std::max(s.readyAt,detail::after(tick,detail::scaled(p.globalCooldown,out.cooldownMilli)));s.pendingAttack=0;s.pendingKey=0;
    }
    s.busy=in.busy;
    for(std::size_t i=0;i<p.adds.size();++i){
        const auto& a=p.adds[i];if(tick<s.addNext[i])continue;
        uint32_t alive=i<in.aliveAdds.size()?in.aliveAdds[i]:0;uint32_t count=a.count;
        if(a.maxAlive)count=alive>=a.maxAlive?0:std::min(count,a.maxAlive-alive);
        if(count)out.events.push_back({BossEventKind::SpawnAdds,int64_t(i),a.unit,count,a.minRadius,a.maxRadius});
        s.addNext[i]=a.every?detail::after(tick,a.every):std::numeric_limits<uint32_t>::max();
    }
    out.transitioning=s.phase>0&&uint64_t(tick)<uint64_t(s.phaseStarted)+p.transitionTicks;
    if(!in.busy&&!s.pendingAttack&&tick>=s.readyAt&&in.targetDistance&&!p.pattern.empty()){
        auto dist=*in.targetDistance;
        auto ready=[&](const BossPatternEntry& x){
            auto r=s.attackReady.find(x.attack);bool inBand=dist>=x.minRange&&(x.maxRange.raw==0||dist<=x.maxRange);
            return inBand&&(r==s.attackReady.end()||tick>=r->second);
        };
        const BossPatternEntry* pick=nullptr;
        if(p.mode==PatternMode::ByRange){for(const auto& x:p.pattern)if(ready(x)){pick=&x;break;}}
        else if(p.mode==PatternMode::Sequence){const auto& x=p.pattern[s.cursor%p.pattern.size()];if(ready(x)){pick=&x;s.cursor=uint32_t((s.cursor+1)%p.pattern.size());}}
        else{
            uint64_t total=0;std::size_t eligible=0;for(const auto& x:p.pattern)if(ready(x)){total+=x.weight;++eligible;}
            if(eligible==1){for(const auto& x:p.pattern)if(ready(x))pick=&x;}
            else if(total>0){uint64_t roll=rng.below(uint32_t(std::min<uint64_t>(total,std::numeric_limits<uint32_t>::max())));for(const auto& x:p.pattern)if(ready(x)){if(roll<x.weight){pick=&x;break;}roll-=x.weight;}}
        }
        if(pick){
            auto n=s.uses[pick->attack]++;auto attack=pick->alternate&&n%2==1?pick->alternate:pick->attack;
            // Alternating entries share one cooldown key: the pattern entry's own attack.
            s.pendingAttack=attack;s.pendingKey=pick->attack;
            s.busy=true;out.attack=attack;out.events.push_back({BossEventKind::Attack,attack});
        }
    }
    state=std::move(s);return out;
}
// Telegraph damage scaled by the phase / enrage multiplier of this step.
inline int64_t bossDamage(const BossStep& step,int64_t damage){return detail::mulMilli(damage,step.damageMilli);}
}
