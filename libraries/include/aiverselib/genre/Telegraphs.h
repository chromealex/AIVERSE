#pragma once
// Telegraphed area attacks (boss-encounters): windup -> active -> recovery on integer ticks, ground areas
// (circle, cone, line, ring) with fixed-point hit tests against target circles, one or periodic strikes with
// damage falloff and status ids reported on the exact strike tick, and view records for progress telegraphs.
// Outcomes only: health, statuses and movement stay with the owner (StatusEffects.h is an optional custom owner).
#include "CombatActions.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kTelegraphMaxTargets=4096,kTelegraphMaxStatuses=16;
inline constexpr uint32_t kTelegraphMaxTicks=36000;

// ---- Ground areas. XZ plane stored as Vec2 {x, z}; yaw in turns, yaw 0 faces +Z, +X is right (CombatActions.h).
enum class AreaShape:uint8_t {Circle,Cone,Line,Ring};
// Circle/Ring: centre = origin, radius (Ring: inner..radius). Cone: apex = origin, radius, half angle in turns.
// Line: rectangle from origin forward over length, halfWidth to each side (charges, beams, waves).
struct AreaDef {AreaShape shape=AreaShape::Circle;Fixed radius=1_fx,inner{},halfAngle=Fixed::fromRatio(1,8),length=1_fx,halfWidth=Fixed::fromRatio(1,2);};
struct AreaPose {Vec2 origin{};Fixed yaw{};};
inline Vec2 areaForward(Fixed yaw){return {fxSinTurns(yaw),fxCosTurns(yaw)};}
inline Vec2 areaRight(Fixed yaw){return {fxCosTurns(yaw),-fxSinTurns(yaw)};}
inline Fixed yawTowards(Vec2 d,Fixed fallback){return d.x.raw==0&&d.y.raw==0?fallback:fxAtan2Turns(d.x,d.y);}
inline bool areaBounded(Vec2 p){return combatBounded(Vec3{p.x,{},p.y},kCombatCoordLimit);}
inline bool valid(const AreaDef& a){
    auto size=[](Fixed v){return v>Fixed{}&&v<=kCombatSizeLimit;};
    switch(a.shape){
    case AreaShape::Circle:return size(a.radius);
    case AreaShape::Ring:return size(a.radius)&&a.inner>=Fixed{}&&a.inner<a.radius;
    case AreaShape::Cone:return size(a.radius)&&a.halfAngle>Fixed{}&&a.halfAngle<=Fixed::fromRatio(1,2);
    case AreaShape::Line:return size(a.length)&&size(a.halfWidth);
    }
    return false;
}
inline bool valid(const AreaPose& p){return areaBounded(p.origin)&&p.yaw>= -1_fx&&p.yaw<=1_fx;}
namespace detail {
inline Fixed areaSegmentSq(Vec2 p,Vec2 a,Vec2 b){
    auto d=b-a;auto len=d.lengthSq();if(len.raw<=0)return (p-a).lengthSq();
    auto t=fxClamp((p-a).dot(d)/len,Fixed{},1_fx);auto q=a+d*t;return (p-q).lengthSq();
}
}
// Does a target circle (centre p, radius r >= 0) touch the area? Touching counts. `reach` scales a Line's length
// (sweeps grow with the active window); 1 = full length. Invalid data never hits.
inline bool areaHits(const AreaDef& a,const AreaPose& pose,Vec2 p,Fixed r,Fixed reach=1_fx){
    if(!valid(a)||!valid(pose)||!areaBounded(p)||r<Fixed{}||r>kCombatSizeLimit)return false;
    auto d=p-pose.origin;auto distSq=d.lengthSq();
    switch(a.shape){
    case AreaShape::Circle:{auto out=a.radius+r;return distSq<=out*out;}
    case AreaShape::Ring:{auto out=a.radius+r;if(distSq>out*out)return false;auto in=a.inner-r;return in<=Fixed{}||distSq>=in*in;}
    case AreaShape::Cone:{
        if(distSq<=r*r)return true;
        auto out=a.radius+r;if(distSq>out*out)return false;
        if(fxAbs(wrapTurns(yawTowards(d,pose.yaw)-pose.yaw))<=a.halfAngle)return true;
        auto left=pose.origin+areaForward(pose.yaw-a.halfAngle)*a.radius,right=pose.origin+areaForward(pose.yaw+a.halfAngle)*a.radius;
        return detail::areaSegmentSq(p,pose.origin,left)<=r*r||detail::areaSegmentSq(p,pose.origin,right)<=r*r;
    }
    case AreaShape::Line:{
        auto length=a.length*fxClamp(reach,Fixed{},1_fx);
        auto f=d.dot(areaForward(pose.yaw)),s=d.dot(areaRight(pose.yaw));
        auto ex=f-fxClamp(f,Fixed{},length),ey=s-fxClamp(s,-a.halfWidth,a.halfWidth);return ex*ex+ey*ey<=r*r;
    }
    }
    return false;
}
// 0 at the centre / apex / line start .. 1000 at the outer edge (Ring: always 0). Used for damage falloff.
inline int64_t areaFalloffMilli(const AreaDef& a,const AreaPose& pose,Vec2 p){
    if(!valid(a))return 0;
    auto d=p-pose.origin;
    Fixed t{};
    if(a.shape==AreaShape::Line)t=d.dot(areaForward(pose.yaw))/a.length;
    else if(a.shape!=AreaShape::Ring)t=d.length()/a.radius;
    return (fxClamp(t,Fixed{},1_fx)*1000).toInt();
}

// ---- Telegraphed attacks.
// Source: the area sits at the caster (+ caster-local offset: x right, y forward), facing the aim (cones, lines,
// rings, slams from a fist). Aim: the area centre is the aimed point, clamped to [minRange, maxRange] from the
// caster (maxRange 0: unclamped) along the aim direction (circle slams, thrown rocks, mortar marks).
enum class AreaAnchor:uint8_t {Source,Aim};
struct TelegraphDef {
    int64_t id=0;AreaDef area;AreaAnchor anchor=AreaAnchor::Source;Vec2 offset{};
    uint32_t windup=10,active=1,recovery=10;   // ticks; the first active tick is the release
    uint32_t interval=0;                       // 0: one strike at release; >0: strike every interval ticks while active
    uint32_t cooldown=0;                       // own cooldown after the attack ends (BossEncounter.h reads it)
    int64_t damage=0,damageMin=0;              // damageMin applies at the edge when falloff is on
    bool falloff=false,oncePerTarget=true,trackAim=false,sweep=false,cancelOnSourceDeath=true,friendlyFire=false;
    Fixed minRange{},maxRange{};
    std::vector<int64_t> statuses;             // status ids reported with every hit (StatusEffects.h or the engine's)
};
using TelegraphSet=std::map<int64_t,TelegraphDef>;
inline uint32_t telegraphLength(const TelegraphDef& d){return d.windup+d.active+d.recovery;}
inline bool valid(const TelegraphDef& d){
    if(d.id<=0||!valid(d.area)||!areaBounded(d.offset)||d.active==0||d.windup>kTelegraphMaxTicks||d.active>kTelegraphMaxTicks||d.recovery>kTelegraphMaxTicks
        ||d.interval>d.active||d.cooldown>kTelegraphMaxTicks||d.damage<0||d.damage>1000000000||d.damageMin<0||d.damageMin>d.damage
        ||d.minRange<Fixed{}||d.maxRange<Fixed{}||d.minRange>kCombatCoordLimit||d.maxRange>kCombatCoordLimit||(d.maxRange>Fixed{}&&d.minRange>d.maxRange)
        ||d.statuses.size()>kTelegraphMaxStatuses||(d.sweep&&d.area.shape!=AreaShape::Line))return false;
    for(auto s:d.statuses)if(s<=0)return false;
    return true;
}
inline bool valid(const TelegraphSet& defs){
    if(defs.empty()||defs.size()>4096)return false;
    for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;
    return true;
}
struct CasterPose {Vec2 position{};Fixed yaw{};bool alive=true;};
struct TelegraphTarget {int64_t id=0,team=0;Vec2 position{};Fixed radius{};bool targetable=true;}; // team 0 = unaffiliated
// One running attack. Keep a vector of these per caster when attacks overlap (rocks in flight + a new swing).
struct TelegraphInstance {
    int64_t id=0,attack=0,source=0,team=0,target=0;
    uint32_t started=0,lastTick=0,lastStrike=0;
    AreaPose pose;Vec2 caster{};
    bool live=false,stepped=false,released=false,struck=false;
    std::vector<int64_t> hit;                  // sorted target ids already struck (oncePerTarget)
};
enum class TelegraphEvent:uint8_t {Started,Released,Strike,Ended,Cancelled};
struct TelegraphHit {int64_t target=0,damage=0,falloffMilli=0;std::vector<int64_t> statuses;};
// Presentation record: draw `shape` at `pose`; `progressMilli` fills the warning during windup (1000 from release).
struct TelegraphView {
    int64_t instance=0,attack=0,source=0,team=0;AreaShape shape=AreaShape::Circle;AreaPose pose;
    Fixed radius{},inner{},halfAngle{},length{},halfWidth{};AttackPhase phase=AttackPhase::Idle;
    int64_t progressMilli=0,activeMilli=0;bool live=false;
};
struct TelegraphStep {AttackPhase phase=AttackPhase::Idle;std::vector<TelegraphEvent> events;std::vector<TelegraphHit> hits;TelegraphView view;};

namespace detail {
inline AreaPose aimPose(const TelegraphDef& d,Vec2 caster,Fixed casterYaw,Vec2 aim){
    auto dir=aim-caster;auto yaw=yawTowards(dir,casterYaw);AreaPose p;p.yaw=yaw;
    if(d.anchor==AreaAnchor::Source){auto f=areaForward(yaw),r=areaRight(yaw);p.origin=caster+r*d.offset.x+f*d.offset.y;return p;}
    auto dist=dir.length();if(d.maxRange>Fixed{})dist=fxClamp(dist,d.minRange,d.maxRange);else dist=fxMax(dist,d.minRange);
    p.origin=caster+areaForward(yaw)*dist;return p;
}
inline bool telegraphTargetsValid(std::span<const TelegraphTarget> t){
    if(t.size()>kTelegraphMaxTargets)return false;
    for(const auto& x:t)if(x.id<=0||x.team<0||!areaBounded(x.position)||x.radius<Fixed{}||x.radius>kCombatSizeLimit)return false;
    std::vector<int64_t> ids;ids.reserve(t.size());for(const auto& x:t)ids.push_back(x.id);std::sort(ids.begin(),ids.end());
    return std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
}
// Starts an attack at `tick` toward `aim` (frozen unless trackAim). The first stepTelegraph call may use this tick.
inline std::optional<TelegraphInstance> beginTelegraph(const TelegraphSet& defs,int64_t attack,int64_t instance,uint32_t tick,int64_t source,int64_t team,
    const CasterPose& caster,Vec2 aim,int64_t target=0){
    auto it=defs.find(attack);
    if(it==defs.end()||!valid(it->second)||instance<=0||source<=0||team<0||target<0||!caster.alive||!areaBounded(caster.position)||!areaBounded(aim)
        ||caster.yaw< -1_fx||caster.yaw>1_fx||uint64_t(tick)+telegraphLength(it->second)>std::numeric_limits<uint32_t>::max())return std::nullopt;
    TelegraphInstance s;s.id=instance;s.attack=attack;s.source=source;s.team=team;s.target=target;s.started=tick;s.lastTick=tick;s.live=true;
    s.caster=caster.position;s.pose=detail::aimPose(it->second,caster.position,caster.yaw,aim);
    if(!valid(s.pose))return std::nullopt;
    return s;
}
inline TelegraphView telegraphView(const TelegraphSet& defs,const TelegraphInstance& s,uint32_t tick){
    TelegraphView v;auto it=defs.find(s.attack);if(it==defs.end()||tick<s.started)return v;const auto& d=it->second;
    v.instance=s.id;v.attack=s.attack;v.source=s.source;v.team=s.team;v.shape=d.area.shape;v.pose=s.pose;v.radius=d.area.radius;v.inner=d.area.inner;
    v.halfAngle=d.area.halfAngle;v.length=d.area.length;v.halfWidth=d.area.halfWidth;v.live=s.live;
    auto age=tick-s.started;
    if(age<d.windup){v.phase=AttackPhase::Windup;v.progressMilli=int64_t(age)*1000/d.windup;}
    else if(age<d.windup+d.active){v.phase=AttackPhase::Active;v.progressMilli=1000;v.activeMilli=int64_t(age-d.windup+1)*1000/d.active;}
    else if(age<telegraphLength(d)){v.phase=AttackPhase::Recovery;v.progressMilli=1000;v.activeMilli=1000;}
    else {v.phase=AttackPhase::Idle;v.progressMilli=1000;v.activeMilli=1000;v.live=false;}
    return v;
}
// Once per tick per instance. `aim` (optional) re-aims a trackAim attack during windup; `targets` are the hittable
// bodies this tick (any order; ids unique). Strikes are reported in ascending target id. A step that jumps past the
// active window still strikes once (late release) so no strike is lost. Returns nullopt (state unchanged) on invalid
// input or a repeated/backwards tick. After Ended/Cancelled the instance is no longer live.
inline std::optional<TelegraphStep> stepTelegraph(const TelegraphSet& defs,TelegraphInstance& state,uint32_t tick,const CasterPose& caster,
    std::optional<Vec2> aim,std::span<const TelegraphTarget> targets){
    auto it=defs.find(state.attack);
    if(it==defs.end()||!valid(it->second)||!state.live||tick<state.started||(state.stepped&&tick<=state.lastTick)||!areaBounded(caster.position)
        ||(aim&&!areaBounded(*aim))||!detail::telegraphTargetsValid(targets))return std::nullopt;
    const auto& d=it->second;auto s=state;TelegraphStep out;
    if(!s.stepped)out.events.push_back(TelegraphEvent::Started);
    s.stepped=true;s.lastTick=tick;auto age=tick-s.started;
    bool sourceGone=!caster.alive&&d.cancelOnSourceDeath&&(!s.released||d.sweep)&&age<d.windup+d.active;
    if(sourceGone){
        s.live=false;out.events.push_back(TelegraphEvent::Cancelled);out.phase=AttackPhase::Idle;
        out.view=telegraphView(defs,s,tick);out.view.live=false;state=std::move(s);return out;
    }
    if(age<d.windup){
        // Tracking follows the caster; a Source area keeps its last yaw when no new aim is given.
        if(d.trackAim&&caster.alive&&(aim||d.anchor==AreaAnchor::Source)){
            auto a=aim?*aim:caster.position+areaForward(s.pose.yaw);s.caster=caster.position;s.pose=detail::aimPose(d,caster.position,caster.yaw,a);
            if(!valid(s.pose))return std::nullopt;
        }
    }
    auto releaseAt=d.windup;bool strike=false;
    if(age>=releaseAt&&!s.released){s.released=true;out.events.push_back(TelegraphEvent::Released);}
    if(s.released){
        if(age<d.windup+d.active){
            if(d.sweep)strike=true;
            else if(!s.struck)strike=true;
            else if(d.interval>0&&age-s.lastStrike>=d.interval)strike=true;
        }else if(!s.struck)strike=true;   // late release: the step skipped the active window
    }
    if(strike){
        out.events.push_back(TelegraphEvent::Strike);s.struck=true;s.lastStrike=age;
        Fixed reach=1_fx;if(d.sweep)reach=Fixed::fromRatio(int64_t(std::min(age-d.windup+1,d.active)),d.active);
        std::vector<std::size_t> order(targets.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return targets[a].id<targets[b].id;});
        for(auto i:order){
            const auto& t=targets[i];
            if(!t.targetable||t.id==s.source||(!d.friendlyFire&&s.team!=0&&t.team==s.team))continue;
            auto at=std::lower_bound(s.hit.begin(),s.hit.end(),t.id);bool seen=at!=s.hit.end()&&*at==t.id;
            if(d.oncePerTarget&&seen)continue;
            if(!areaHits(d.area,s.pose,t.position,t.radius,reach))continue;
            TelegraphHit h;h.target=t.id;h.statuses=d.statuses;
            if(d.falloff){h.falloffMilli=areaFalloffMilli(d.area,s.pose,t.position);h.damage=d.damage-(d.damage-d.damageMin)*h.falloffMilli/1000;}
            else h.damage=d.damage;
            if(!seen){if(s.hit.size()>=kTelegraphMaxTargets)continue;s.hit.insert(at,t.id);}
            out.hits.push_back(std::move(h));
        }
    }
    if(age>=telegraphLength(d)){s.live=false;out.events.push_back(TelegraphEvent::Ended);}
    out.view=telegraphView(defs,s,tick);out.phase=out.view.phase;
    state=std::move(s);return out;
}
// Charge helper: how far along the line a sweeping caster should be at this tick (0..length); the owner moves it.
inline Fixed sweepReach(const TelegraphSet& defs,const TelegraphInstance& s,uint32_t tick){
    auto it=defs.find(s.attack);if(it==defs.end()||!it->second.sweep||tick<s.started+it->second.windup)return {};
    const auto& d=it->second;return d.area.length*int64_t(std::min(tick-s.started-d.windup+1,d.active))/int64_t(d.active);
}
// Should the caster stand still? Windup and recovery freeze the pose (Drone Fall's attack pose lock); a sweep moves.
inline bool telegraphLocksMovement(const TelegraphSet& defs,const TelegraphInstance& s,uint32_t tick){
    auto it=defs.find(s.attack);if(it==defs.end()||!s.live||tick<s.started)return false;
    auto age=tick-s.started;const auto& d=it->second;return age<telegraphLength(d)&&!(d.sweep&&age>=d.windup&&age<d.windup+d.active);
}
}
