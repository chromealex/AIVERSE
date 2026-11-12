#pragma once
// Combat actions composed over Melee.h: frame-keyed hitboxes/hurtboxes, dodge invulnerability,
// directional guard with a perfect-parry window, poise/stagger and lock-on target selection.
// Policies return outcomes; damage, stamina, health and statuses stay with the existing combat owner.
#include "Melee.h"
#include "engine/core/Archive.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Hash.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kCombatCoordLimit=Fixed::fromInt(100000);
inline constexpr Fixed kCombatSizeLimit=Fixed::fromInt(1000);
inline constexpr std::size_t kCombatMaxShapes=64,kCombatMaxBodies=4096;

// ---- Shapes. Positions are Fixed metres; yaw is in turns around +Y, yaw 0 faces +Z, +X is right.
enum class ShapeKind:uint8_t {Sphere,Capsule,Box};
// Sphere: centre a. Capsule: segment a..b plus radius. Box: centre a, half extents, rotated by yaw only (radius 0).
struct CombatShape {ShapeKind kind=ShapeKind::Sphere;Vec3 a{},b{},half{};Fixed radius{},yaw{};};
struct CombatPose {Vec3 position{};Fixed yaw{};};
inline Fixed wrapTurns(Fixed t){int64_t r=t.raw%Fixed::kOne;if(r>Fixed::kOne/2)r-=Fixed::kOne;else if(r<= -Fixed::kOne/2)r+=Fixed::kOne;return Fixed::fromRaw(r);}
inline Fixed yawOf(Vec3 d){return fxAtan2Turns(d.x,d.z);}
inline Vec3 rotateYaw(Vec3 v,Fixed yaw){auto c=fxCosTurns(yaw),s=fxSinTurns(yaw);return {v.x*c+v.z*s,v.y,v.z*c-v.x*s};}
inline bool combatBounded(Vec3 p,Fixed m){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}
inline bool valid(const CombatShape& s){
    if(uint8_t(s.kind)>uint8_t(ShapeKind::Box)||!combatBounded(s.a,kCombatCoordLimit)||!combatBounded(s.b,kCombatCoordLimit)||s.radius<Fixed{}||s.radius>kCombatSizeLimit||s.yaw< -1_fx||s.yaw>1_fx)return false;
    if(s.kind==ShapeKind::Box)return s.radius==Fixed{}&&s.half.x>=Fixed{}&&s.half.y>=Fixed{}&&s.half.z>=Fixed{}&&combatBounded(s.half,kCombatSizeLimit);
    return true;
}
inline bool valid(const CombatPose& p){return combatBounded(p.position,kCombatCoordLimit)&&p.yaw>= -1_fx&&p.yaw<=1_fx;}
inline CombatShape toWorld(const CombatShape& s,const CombatPose& p){
    auto w=s;w.a=p.position+rotateYaw(s.a,p.yaw);w.b=p.position+rotateYaw(s.b,p.yaw);w.yaw=wrapTurns(s.yaw+p.yaw);return w;
}
namespace detail {
inline Fixed pointSegmentSq(Vec3 p,Vec3 a,Vec3 b){
    auto d=b-a;auto len=d.lengthSq();if(len.raw<=0)return (p-a).lengthSq();
    auto t=fxClamp((p-a).dot(d)/len,Fixed{},1_fx);return (p-(a+d*t)).lengthSq();
}
inline Fixed pointBoxSq(Vec3 p,const CombatShape& box){
    auto l=rotateYaw(p-box.a,-box.yaw);Vec3 c{fxClamp(l.x,-box.half.x,box.half.x),fxClamp(l.y,-box.half.y,box.half.y),fxClamp(l.z,-box.half.z,box.half.z)};return (l-c).lengthSq();
}
// Squared distance from a point to the shape core (centre, segment or box); convex in the point.
inline Fixed pointCoreSq(Vec3 p,const CombatShape& s){
    if(s.kind==ShapeKind::Sphere)return (p-s.a).lengthSq();
    if(s.kind==ShapeKind::Capsule)return pointSegmentSq(p,s.a,s.b);
    return pointBoxSq(p,s);
}
// Deterministic integer ternary search over the segment parameter; the distance to a convex core is convex.
inline Fixed segmentCoreSq(Vec3 a,Vec3 b,const CombatShape& other){
    auto at=[&](int64_t raw){return pointCoreSq(a+(b-a)*Fixed::fromRaw(raw),other);};
    int64_t lo=0,hi=Fixed::kOne;
    while(hi-lo>2){int64_t m1=lo+(hi-lo)/3,m2=hi-(hi-lo)/3;if(at(m1)<=at(m2))hi=m2;else lo=m1;}
    auto best=fxMin(at(0),at(Fixed::kOne));for(auto t=lo;t<=hi;++t)best=fxMin(best,at(t));return best;
}
inline bool boxesOverlap(const CombatShape& a,const CombatShape& b){
    auto d=b.a-a.a;if(fxAbs(d.y)>a.half.y+b.half.y)return false;
    auto ca=fxCosTurns(a.yaw),sa=fxSinTurns(a.yaw),cb=fxCosTurns(b.yaw),sb=fxSinTurns(b.yaw);
    // Local x/z axes projected onto the ground plane as (x,z).
    const Vec2 axes[4]={{ca,-sa},{sa,ca},{cb,-sb},{sb,cb}};
    for(auto n:axes){
        auto ra=a.half.x*fxAbs(axes[0].dot(n))+a.half.z*fxAbs(axes[1].dot(n));
        auto rb=b.half.x*fxAbs(axes[2].dot(n))+b.half.z*fxAbs(axes[3].dot(n));
        if(fxAbs(Vec2{d.x,d.z}.dot(n))>ra+rb)return false;
    }return true;
}
}
// World-space overlap test; touching counts as overlapping. Invalid shapes never overlap.
inline bool overlaps(const CombatShape& x,const CombatShape& y){
    if(!valid(x)||!valid(y))return false;
    if(x.kind==ShapeKind::Box&&y.kind==ShapeKind::Box)return detail::boxesOverlap(x,y);
    const auto& p=x.kind==ShapeKind::Box?y:x;const auto& q=x.kind==ShapeKind::Box?x:y;
    auto reach=p.radius+q.radius;auto limit=reach*reach;
    if(p.kind==ShapeKind::Sphere)return detail::pointCoreSq(p.a,q)<=limit;
    return detail::segmentCoreSq(p.a,p.b,q)<=limit;
}

// ---- Hitboxes keyed to attack frames (age since attack start), hurtboxes per body.
struct HitboxDef {int64_t id=0;uint32_t from=0,to=0;CombatShape shape;}; // active during [from,to)
using HitboxSet=std::map<int64_t,std::vector<HitboxDef>>;              // attack id -> hitboxes by ascending id
struct Hurtbox {int64_t id=0;CombatShape shape;};
struct CombatBody {int64_t id=0,team=0;CombatPose pose;std::vector<Hurtbox> hurtboxes;}; // team 0 = unaffiliated
struct HitContact {int64_t target=0,hitbox=0,hurtbox=0;};
inline bool valid(const AttackSet& defs,const HitboxSet& boxes){
    if(!valid(defs)||boxes.size()>defs.size())return false;
    for(const auto& [attack,list]:boxes){
        auto it=defs.find(attack);if(it==defs.end()||list.empty()||list.size()>kCombatMaxShapes)return false;const auto& d=it->second;int64_t previous=0;
        for(const auto& h:list){if(h.id<=previous||h.from>=h.to||h.from<d.windup||h.to>d.windup+d.active||!valid(h.shape))return false;previous=h.id;}
    }return true;
}
inline bool valid(const CombatBody& b,bool requireHurtboxes=true){
    if(b.id<=0||b.team<0||!valid(b.pose)||b.hurtboxes.size()>kCombatMaxShapes||(requireHurtboxes&&b.hurtboxes.empty()))return false;
    int64_t previous=0;for(const auto& h:b.hurtboxes){if(h.id<=previous||!valid(h.shape))return false;previous=h.id;}return true;
}
// World shapes of the hitboxes live on this tick; empty outside the active phase.
inline std::vector<std::pair<int64_t,CombatShape>> activeHitboxes(const AttackSet& defs,const HitboxSet& boxes,const MeleeState& s,uint32_t tick,const CombatPose& pose){
    std::vector<std::pair<int64_t,CombatShape>> out;auto it=boxes.find(s.attack);
    if(it==boxes.end()||attackPhase(defs,s,tick)!=AttackPhase::Active)return out;
    auto age=tick-s.started;
    for(const auto& h:it->second)if(age>=h.from&&age<h.to)out.emplace_back(h.id,toWorld(h.shape,pose));
    return out;
}
// Overlap + self/team filter + once-per-swing bookkeeping through recordMeleeHit. Contacts are sorted by
// target ID; each names the first overlapping hitbox/hurtbox pair by ascending IDs. Invalid input -> nullopt,
// state unchanged. Feed each contact to resolveCombatContact and the existing damage owner.
inline std::optional<std::vector<HitContact>> collectMeleeHits(const AttackSet& defs,const HitboxSet& boxes,MeleeState& state,uint32_t tick,
    const CombatBody& attacker,std::span<const CombatBody> targets,bool friendlyFire=false){
    if(!valid(defs,boxes)||!valid(attacker,false)||targets.size()>kCombatMaxBodies)return std::nullopt;
    std::vector<std::size_t> order(targets.size());for(std::size_t i=0;i<order.size();++i){order[i]=i;if(!valid(targets[i]))return std::nullopt;}
    std::sort(order.begin(),order.end(),[&](auto x,auto y){return targets[x].id<targets[y].id;});
    for(std::size_t i=1;i<order.size();++i)if(targets[order[i]].id==targets[order[i-1]].id)return std::nullopt;
    std::vector<HitContact> out;auto live=activeHitboxes(defs,boxes,state,tick,attacker.pose);if(live.empty())return out;
    auto s=state;
    for(auto index:order){
        const auto& t=targets[index];
        if(t.id==attacker.id||(!friendlyFire&&attacker.team!=0&&attacker.team==t.team)||std::binary_search(s.hitTargets.begin(),s.hitTargets.end(),t.id))continue;
        std::optional<HitContact> found;
        for(const auto& [hitbox,shape]:live){
            for(const auto& hurt:t.hurtboxes)if(overlaps(shape,toWorld(hurt.shape,t.pose))){found=HitContact{t.id,hitbox,hurt.id};break;}
            if(found)break;
        }
        if(found&&recordMeleeHit(defs,s,tick,t.id))out.push_back(*found);
    }
    state=std::move(s);return out;
}

// ---- Dodge: invulnerability window [invulnFrom,invulnTo) relative to start; cooldown counts from the dodge end.
struct DodgeConfig {uint32_t duration=12,invulnFrom=2,invulnTo=8,cooldown=20;};
struct DodgeState {uint32_t started=0,readyAt=0,lastTick=0;bool dodged=false,initialized=false;};
inline bool valid(const DodgeConfig& c){return c.duration>0&&c.duration<=3600&&c.invulnFrom<=c.invulnTo&&c.invulnTo<=c.duration&&c.cooldown<=36000;}
inline bool dodging(const DodgeConfig& c,const DodgeState& s,uint32_t tick){return s.dodged&&tick>=s.started&&tick-s.started<c.duration;}
inline bool dodgeInvulnerable(const DodgeConfig& c,const DodgeState& s,uint32_t tick){
    if(!s.dodged||tick<s.started)return false;
    auto age=tick-s.started;return age>=c.invulnFrom&&age<c.invulnTo;
}
// nullopt: invalid config or repeated/backwards tick. true: a dodge started this tick. Authorization
// (stamina, stagger, attack phase) runs only when a start is possible; spend atomically inside it.
template<class Authorize>
std::optional<bool> stepDodge(const DodgeConfig& c,DodgeState& s,uint32_t tick,bool requested,Authorize authorize){
    if(!valid(c)||(s.initialized&&tick<=s.lastTick))return std::nullopt;
    auto next=s;next.initialized=true;next.lastTick=tick;bool started=false;
    uint64_t ready=uint64_t(tick)+c.duration+c.cooldown;
    if(requested&&(!s.dodged||tick>=s.readyAt)&&!dodging(c,s,tick)&&ready<=std::numeric_limits<uint32_t>::max()&&authorize()){
        next.dodged=true;next.started=tick;next.readyAt=uint32_t(ready);started=true;
    }
    s=next;return started;
}

// ---- Guard: directional block arc (half angle in turns) and a perfect-parry window opened by raising the guard.
// A re-raise sooner than rearmTicks after the previous raise blocks but cannot parry (no parry spam).
struct GuardConfig {Fixed halfArc=Fixed::fromRatio(1,8);uint32_t parryTicks=6,rearmTicks=20;};
struct GuardState {uint32_t raisedAt=0,lastTick=0;bool raised=false,parryArmed=false,everRaised=false,initialized=false;};
inline bool valid(const GuardConfig& c){return c.halfArc>=Fixed{}&&c.halfArc<=Fixed::fromRatio(1,2)&&c.parryTicks<=3600&&c.rearmTicks<=36000;}
inline bool stepGuard(const GuardConfig& c,GuardState& s,uint32_t tick,bool held){
    if(!valid(c)||(s.initialized&&tick<=s.lastTick))return false;
    auto next=s;next.initialized=true;next.lastTick=tick;
    if(held&&!s.raised){next.raised=true;next.parryArmed=!s.everRaised||tick-s.raisedAt>=c.rearmTicks;next.everRaised=true;next.raisedAt=tick;}
    else if(!held)next.raised=false;
    s=next;return true;
}
inline bool insideGuardArc(const GuardConfig& c,const CombatPose& defender,Vec3 attacker){
    auto d=attacker-defender.position;if(d.x.raw==0&&d.z.raw==0)return true;
    return fxAbs(wrapTurns(yawOf(d)-defender.yaw))<=c.halfArc;
}
inline bool parryActive(const GuardConfig& c,const GuardState& s,uint32_t tick){return s.raised&&s.parryArmed&&tick>=s.raisedAt&&tick-s.raisedAt<c.parryTicks;}
// Step dodge/guard for this tick first. Priority (from Melee.h): invulnerable > parry > block > hit.
inline MeleeContact resolveCombatContact(const DodgeConfig& dc,const DodgeState& ds,const GuardConfig& gc,const GuardState& gs,
    const CombatPose& defender,Vec3 attackerPosition,uint32_t tick,bool unblockable=false){
    bool inside=!unblockable&&gs.raised&&insideGuardArc(gc,defender,attackerPosition);
    return resolveMeleeContact(dodgeInvulnerable(dc,ds,tick),parryActive(gc,gs,tick),gs.raised,inside);
}

// ---- Poise (integer milli-units): hits/blocks drain poise, reaching zero breaks it (long stagger), then it refills.
struct PoiseConfig {int64_t maximum=100000,recoveryPerTick=1000,blockScaleMilli=500;uint32_t recoveryDelay=30,hitStunTicks=8,blockStunTicks=4,breakStunTicks=45,parryStunTicks=40;};
struct PoiseState {int64_t poise=0;uint32_t lastDamage=0,stunUntil=0,lastTick=0;bool broken=false,damaged=false,initialized=false;};
struct PoiseResult {int64_t poiseDamage=0;bool broke=false;uint32_t stunUntil=0;};
inline bool valid(const PoiseConfig& c){
    return c.maximum>0&&c.maximum<=1000000000&&c.recoveryPerTick>=0&&c.recoveryPerTick<=1000000000&&c.blockScaleMilli>=0&&c.blockScaleMilli<=1000
        &&c.recoveryDelay<=36000&&c.hitStunTicks<=3600&&c.blockStunTicks<=3600&&c.breakStunTicks<=3600&&c.parryStunTicks<=3600;
}
inline bool staggered(const PoiseState& s,uint32_t tick){return tick<s.stunUntil;}
inline uint32_t stunEnd(uint32_t tick,uint32_t ticks){return uint32_t(std::min<uint64_t>(uint64_t(tick)+ticks,std::numeric_limits<uint32_t>::max()));}
// Once per tick before contacts; recovery is exact regardless of how many ticks were skipped.
inline bool stepPoise(const PoiseConfig& c,PoiseState& s,uint32_t tick){
    if(!valid(c)||(s.initialized&&tick<=s.lastTick)||s.poise>c.maximum)return false;
    auto next=s;next.lastTick=tick;
    if(!s.initialized){next.initialized=true;next.poise=c.maximum;}
    else if(s.broken){if(tick>=s.stunUntil){next.broken=false;next.poise=c.maximum;next.damaged=false;}}
    else{
        uint64_t from=std::max<uint64_t>(s.lastTick,s.damaged?uint64_t(s.lastDamage)+c.recoveryDelay:0);
        if(tick>from){auto gain=uint64_t(c.recoveryPerTick)*(tick-from);next.poise=int64_t(std::min<uint64_t>(uint64_t(c.maximum),uint64_t(s.poise)+gain));}
    }
    s=next;return true;
}
// Apply one resolved contact to the defender on the tick stepPoise ran. Parried/Evaded deal no poise damage;
// apply the parry stagger to the attacker with applyParryStagger. damage is the owner's poise damage (milli).
inline std::optional<PoiseResult> applyPoiseHit(const PoiseConfig& c,PoiseState& s,uint32_t tick,int64_t damage,MeleeContact contact){
    if(!valid(c)||!s.initialized||s.lastTick!=tick||damage<0||damage>1000000000||uint8_t(contact)>uint8_t(MeleeContact::Evaded))return std::nullopt;
    PoiseResult r;uint32_t stun=0;
    if(contact==MeleeContact::Hit){r.poiseDamage=damage;stun=c.hitStunTicks;}
    else if(contact==MeleeContact::Blocked){r.poiseDamage=damage*c.blockScaleMilli/1000;stun=c.blockStunTicks;}
    if(s.broken){r.poiseDamage=0;r.stunUntil=s.stunUntil;return r;}
    if(r.poiseDamage>0){s.poise-=r.poiseDamage;s.damaged=true;s.lastDamage=tick;}
    if(s.poise<=0){s.poise=0;s.broken=true;r.broke=true;stun=std::max(stun,c.breakStunTicks);}
    if(stun)s.stunUntil=std::max(s.stunUntil,stunEnd(tick,stun));
    r.stunUntil=s.stunUntil;return r;
}
inline bool applyParryStagger(const PoiseConfig& c,PoiseState& attacker,uint32_t tick){
    if(!valid(c)||!attacker.initialized||attacker.lastTick!=tick)return false;
    attacker.stunUntil=std::max(attacker.stunUntil,stunEnd(tick,c.parryStunTicks));return true;
}
// Stagger interrupts the current swing and its buffered follow-up; hit bookkeeping restarts with the next attack.
inline void interruptMelee(MeleeState& s){s.attack=0;s.queued=0;s.hitTargets.clear();}

// ---- Lock-on: score = distance/range*distanceWeight + |angle|/halfAngle*angleWeight (lower wins, then smaller ID).
struct LockOnConfig {Fixed range=15_fx,breakRange=20_fx,halfAngle=Fixed::fromRatio(1,4);int64_t distanceWeight=1000,angleWeight=1000;};
struct LockCandidate {int64_t id=0;Vec3 position{};bool targetable=true;}; // targetable = visibility/alive from the owner
struct LockOnState {int64_t target=0;uint32_t lastTick=0;bool initialized=false;};
struct LockCommand {bool toggle=false;int switchDirection=0;}; // -1 left, +1 right
struct LockOnResult {int64_t target=0;bool changed=false,broken=false;};
inline bool valid(const LockOnConfig& c){
    return c.range>Fixed{}&&c.range<=c.breakRange&&c.breakRange<=kCombatCoordLimit&&c.halfAngle>Fixed{}&&c.halfAngle<=Fixed::fromRatio(1,2)
        &&c.distanceWeight>=0&&c.distanceWeight<=1000000&&c.angleWeight>=0&&c.angleWeight<=1000000;
}
inline bool validCandidates(std::span<const LockCandidate> c){
    if(c.size()>kCombatMaxBodies)return false;
    std::vector<int64_t> ids;ids.reserve(c.size());
    for(const auto& x:c){if(x.id<=0||!combatBounded(x.position,kCombatCoordLimit))return false;ids.push_back(x.id);}
    std::sort(ids.begin(),ids.end());return std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
inline std::optional<Fixed> lockScore(const LockOnConfig& c,const CombatPose& viewer,const LockCandidate& x){
    if(!x.targetable)return std::nullopt;
    auto d=x.position-viewer.position;auto distance=d.length();
    if(distance>c.range)return std::nullopt;
    auto angle=(d.x.raw==0&&d.z.raw==0)?Fixed{}:fxAbs(wrapTurns(yawOf(d)-viewer.yaw));if(angle>c.halfAngle)return std::nullopt;
    return (distance/c.range)*c.distanceWeight+(angle/c.halfAngle)*c.angleWeight;
}
inline int64_t selectLockTarget(const LockOnConfig& c,const CombatPose& viewer,std::span<const LockCandidate> candidates){
    int64_t best=0;Fixed bestScore{};
    for(const auto& x:candidates)if(auto s=lockScore(c,viewer,x);s&&(!best||*s<bestScore||(*s==bestScore&&x.id<best))){best=x.id;bestScore=*s;}
    return best;
}
// Once per tick. Break: target missing, untargetable or beyond breakRange. Toggle releases a held target or
// acquires the best one. Switch picks the nearest angular neighbour on that side among scorable candidates
// (no wrap-around), tie-broken by distance then ID; with no neighbour the lock is kept.
inline std::optional<LockOnResult> stepLockOn(const LockOnConfig& c,LockOnState& s,uint32_t tick,const CombatPose& viewer,
    std::span<const LockCandidate> candidates,LockCommand cmd){
    if(!valid(c)||!valid(viewer)||!validCandidates(candidates)||(s.initialized&&tick<=s.lastTick)||s.target<0||cmd.switchDirection< -1||cmd.switchDirection>1)return std::nullopt;
    LockOnResult r;auto previous=s.target;int64_t target=s.target;const LockCandidate* current=nullptr;
    if(target){
        for(const auto& x:candidates)if(x.id==target)current=&x;
        if(!current||!current->targetable||(current->position-viewer.position).lengthSq()>c.breakRange*c.breakRange){target=0;current=nullptr;r.broken=true;}
    }
    if(cmd.toggle){if(current){target=0;current=nullptr;}else target=selectLockTarget(c,viewer,candidates);}
    else if(cmd.switchDirection&&current){
        auto base=yawOf(current->position-viewer.position);const LockCandidate* pick=nullptr;Fixed pickDelta{},pickDistance{};
        for(const auto& x:candidates){
            if(x.id==target||!lockScore(c,viewer,x))continue;
            auto d=x.position-viewer.position;auto delta=wrapTurns(yawOf(d)-base);
            if((cmd.switchDirection>0&&delta<=Fixed{})||(cmd.switchDirection<0&&delta>=Fixed{}))continue;
            auto magnitude=fxAbs(delta);auto distance=d.lengthSq();
            if(!pick||magnitude<pickDelta||(magnitude==pickDelta&&(distance<pickDistance||(distance==pickDistance&&x.id<pick->id)))){pick=&x;pickDelta=magnitude;pickDistance=distance;}
        }
        if(pick)target=pick->id;
    }
    s.target=target;s.lastTick=tick;s.initialized=true;r.target=target;r.changed=target!=previous;return r;
}

// ---- Persistence (records for genre::save/load in Persistence.h; kinds 32..36 are reserved for combat-actions).
struct CombatActorState {DodgeState dodge;GuardState guard;PoiseState poise;LockOnState lock;};
inline void io(Archive& a,DodgeState& s){a(s.started,s.readyAt,s.lastTick,s.dodged,s.initialized);}
inline void io(Archive& a,GuardState& s){a(s.raisedAt,s.lastTick,s.raised,s.parryArmed,s.everRaised,s.initialized);}
inline void io(Archive& a,PoiseState& s){a(s.poise,s.lastDamage,s.stunUntil,s.lastTick,s.broken,s.damaged,s.initialized);}
inline void io(Archive& a,LockOnState& s){a(s.target,s.lastTick,s.initialized);}
inline void io(Archive& a,CombatActorState& s){a(s.dodge,s.guard,s.poise,s.lock);}
inline bool valid(const DodgeState& s){return s.initialized?(!s.dodged||(s.started<=s.lastTick&&s.readyAt>=s.started)):(!s.dodged&&s.lastTick==0);}
inline bool valid(const GuardState& s){return (s.initialized||(!s.raised&&!s.everRaised&&s.lastTick==0))&&(!s.raised||s.everRaised)&&(!s.everRaised||s.raisedAt<=s.lastTick)&&(!s.parryArmed||s.everRaised);}
inline bool valid(const PoiseState& s){return s.poise>=0&&s.poise<=1000000000&&(!s.broken||s.poise==0)&&(!s.damaged||s.lastDamage<=s.lastTick)&&(s.initialized||(s.poise==0&&!s.broken&&!s.damaged&&s.lastTick==0));}
inline bool valid(const LockOnState& s){return s.target>=0&&(s.initialized||(s.target==0&&s.lastTick==0));}
inline bool valid(const CombatActorState& s){return valid(s.dodge)&&valid(s.guard)&&valid(s.poise)&&valid(s.lock);}
inline uint16_t stateKind(const CombatActorState&){return 32;}
inline uint16_t stateKind(const DodgeState&){return 33;}
inline uint16_t stateKind(const GuardState&){return 34;}
inline uint16_t stateKind(const PoiseState&){return 35;}
inline uint16_t stateKind(const LockOnState&){return 36;}
// Feed the canonical record bytes into the simulation checksum (e.g. RtsModule/NativeState hashing).
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
void hashCombatState(Hasher& h,const State& s){std::string bytes;Archive a(&bytes);auto copy=s;uint16_t kind=stateKind(copy);a(kind,copy);h.str(bytes);}
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
uint64_t combatHash(const State& s){Hasher h;hashCombatState(h,s);return h.value();}
}
