#pragma once
// Mob behaviour states (boss-encounters): wander (random legs in a ring around an anchor with delays, a
// walkability callback like Drone Fall's grid raycast), hunt (committed target > priority target such as the
// personal boss's hero > keep the current one within loseRange > nearest within aggroRange), flee at low HP,
// leash return home, and swarm pressure (crowd separation + look-ahead side-stepping of Drone Fall's
// MobAvoidanceJob, in fixed point). Output is a desired XZ direction (length <= 1) and a goal point: feed it to
// the owner's steering, flow field (RTS move orders) or navmesh; this never moves bodies itself.
#include "Telegraphs.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kMobMaxCandidates=4096,kMobMaxNeighbours=256;
enum class MobMode:uint8_t {Idle,Wander,Hunt,Flee,Return};
struct MobBehaviourConfig {
    Fixed aggroRange=12_fx,loseRange=16_fx,stopDistance=Fixed::fromRatio(3,2),switchMargin=1_fx;
    Fixed wanderMin=2_fx,wanderMax=6_fx,arriveRadius=Fixed::fromRatio(1,2),leashRadius{};   // leash 0: off
    uint32_t wanderDelay=20,wanderLegTicks=100,wanderAttempts=10;
    int64_t fleeBelowMilli=0,fleeRecoverMilli=0;Fixed fleeRange=8_fx;uint32_t fleeTicks=60;   // fleeBelowMilli 0: never flees
    bool wander=true;
    // Swarm: spacing = (r1 + r2) * spacingMilli; side-step look-ahead in metres; correction <= desired * correctionMilli.
    int64_t spacingMilli=1100,correctionMilli=750;Fixed lookAhead=1_fx;uint32_t maxNeighbours=8;
};
inline bool valid(const MobBehaviourConfig& c){
    auto len=[](Fixed v){return v>=Fixed{}&&v<=kCombatCoordLimit;};
    return len(c.aggroRange)&&len(c.loseRange)&&c.loseRange>=c.aggroRange&&len(c.stopDistance)&&len(c.switchMargin)&&len(c.wanderMin)&&len(c.wanderMax)
        &&c.wanderMax>=c.wanderMin&&c.arriveRadius>Fixed{}&&len(c.leashRadius)&&c.wanderDelay<=36000&&c.wanderLegTicks>0&&c.wanderLegTicks<=36000
        &&c.wanderAttempts>0&&c.wanderAttempts<=32&&c.fleeBelowMilli>=0&&c.fleeBelowMilli<=1000&&c.fleeRecoverMilli>=c.fleeBelowMilli&&c.fleeRecoverMilli<=1000
        &&len(c.fleeRange)&&c.fleeTicks<=36000&&c.spacingMilli>0&&c.spacingMilli<=10000&&c.correctionMilli>=0&&c.correctionMilli<=1000&&len(c.lookAhead)
        &&c.maxNeighbours<=kMobMaxNeighbours;
}
// committed: target of an attack in progress (kept while it lives); priority: a target this mob prefers (personal
// boss -> its participant's hero); movementLocked: attack pose / stun (direction zero, target kept).
struct MobSelf {int64_t id=0;Vec2 position{},home{},anchor{};Fixed radius=Fixed::fromRatio(1,2);int64_t hpMilli=1000,committed=0,priority=0;bool movementLocked=false;};
struct MobCandidate {int64_t id=0;Vec2 position{};bool hostile=true,targetable=true;};   // visibility-authorized only
struct MobNeighbour {int64_t id=0;Vec2 position{};Fixed radius=Fixed::fromRatio(1,2);};
struct MobState {MobMode mode=MobMode::Idle;int64_t target=0;Vec2 goal{},legFrom{};uint32_t until=0,lastTick=0;bool initialized=false;};
struct MobDecision {MobMode mode=MobMode::Idle;int64_t target=0;Vec2 desired{},goal{},correction{};bool approach=false,switched=false;};
namespace detail {
inline Vec2 normalOr(Vec2 v,Vec2 fallback){auto l=v.length();return l.raw==0?fallback:v/l;}
inline Vec2 capLength(Vec2 v,Fixed m){auto l=v.length();return l>m&&l.raw>0?v*(m/l):v;}
inline bool mobInputsValid(const MobSelf& self,std::span<const MobCandidate> c,std::span<const MobNeighbour> n){
    if(self.id<=0||!areaBounded(self.position)||!areaBounded(self.home)||!areaBounded(self.anchor)||self.radius<Fixed{}||self.radius>kCombatSizeLimit
        ||self.hpMilli<0||self.hpMilli>1000||self.committed<0||self.priority<0||c.size()>kMobMaxCandidates||n.size()>kMobMaxNeighbours)return false;
    std::vector<int64_t> ids;for(const auto& x:c){if(x.id<=0||!areaBounded(x.position))return false;ids.push_back(x.id);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
    for(const auto& x:n)if(x.id<=0||!areaBounded(x.position)||x.radius<Fixed{}||x.radius>kCombatSizeLimit)return false;
    return true;
}
}
// Crowd correction for a desired direction (Drone Fall MobAvoidanceJob): neighbours nearest first (ties by id),
// at most maxNeighbours; coincident pairs split by id without RNG. Returns the correction to add.
inline Vec2 swarmCorrection(const MobBehaviourConfig& c,const MobSelf& self,Vec2 desired,std::span<const MobNeighbour> neighbours){
    auto magnitude=desired.length();if(magnitude.raw==0||c.maxNeighbours==0)return {};
    auto forward=desired/magnitude;Vec2 right{forward.y,-forward.x};
    std::vector<const MobNeighbour*> near;for(const auto& n:neighbours)if(n.id!=self.id)near.push_back(&n);
    std::sort(near.begin(),near.end(),[&](auto a,auto b){auto da=(a->position-self.position).lengthSq(),db=(b->position-self.position).lengthSq();return da<db||(da==db&&a->id<b->id);});
    if(near.size()>c.maxNeighbours)near.resize(c.maxNeighbours);
    Vec2 sum{};const Fixed diag=Fixed::fromRatio(70710678,100000000);
    for(auto n:near){
        auto delta=self.position-n->position;auto distance=delta.length();auto spacing=(self.radius+n->radius)*Fixed::fromRatio(c.spacingMilli,1000);
        if(spacing.raw<=0)continue;
        if(distance.raw==0){auto sign=self.id<n->id?1_fx:-1_fx;sum+=Vec2{diag,diag}*sign;continue;}
        if(distance<spacing)sum+=delta/distance*(1_fx-distance/spacing);
        auto ahead=-delta.dot(forward),lateral=delta.dot(right);
        if(ahead>Fixed{}&&ahead<spacing+c.lookAhead&&fxAbs(lateral)<spacing){
            auto side=fxAbs(lateral)>spacing*Fixed::fromRatio(1,10)?(lateral<Fixed{}?-1_fx:1_fx):1_fx;
            auto fade=c.lookAhead.raw>0?1_fx-fxMax(Fixed{},ahead-spacing)/c.lookAhead:1_fx;
            sum+=right*(side*(1_fx-fxAbs(lateral)/spacing)*fade);
        }
    }
    auto length=sum.length();if(length.raw==0)return {};
    return sum/fxMax(1_fx,length)*(magnitude*Fixed::fromRatio(c.correctionMilli,1000));
}
// Once per mob per tick. walkable(from, to) is the owner's line test (grid raycast / navmesh); wander draws
// rng.unit() twice per attempt. Invalid input or a repeated/backwards tick returns nullopt, state unchanged.
template<class Walkable>
std::optional<MobDecision> stepMob(const MobBehaviourConfig& c,MobState& state,uint32_t tick,const MobSelf& self,std::span<const MobCandidate> candidates,
    std::span<const MobNeighbour> neighbours,Random& rng,Walkable walkable){
    if(!valid(c)||(state.initialized&&tick<=state.lastTick)||!detail::mobInputsValid(self,candidates,neighbours))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;MobDecision out;auto previous=s.target;
    auto find=[&](int64_t id)->const MobCandidate*{if(!id)return nullptr;for(const auto& x:candidates)if(x.id==id&&x.hostile&&x.targetable)return &x;return nullptr;};
    auto distSq=[&](Vec2 p){return (p-self.position).lengthSq();};
    // Leash: return home first; targets are ignored until the mob is back (Drone Fall MobHomeArea.returning).
    if(c.leashRadius>Fixed{}&&(s.mode==MobMode::Return||distSq(self.home)>c.leashRadius*c.leashRadius)&&!find(self.committed)){
        if(distSq(self.home)<=c.arriveRadius*c.arriveRadius){s.mode=MobMode::Idle;s.until=tick+c.wanderDelay;}
        else{s.mode=MobMode::Return;s.target=0;s.goal=self.home;}
    }
    if(s.mode!=MobMode::Return){
        const MobCandidate* nearest=nullptr;
        for(const auto& x:candidates)if(x.hostile&&x.targetable&&distSq(x.position)<=c.aggroRange*c.aggroRange&&(!nearest||distSq(x.position)<distSq(nearest->position)||(distSq(x.position)==distSq(nearest->position)&&x.id<nearest->id)))nearest=&x;
        // Flee: low HP and a hostile close by; ends on recovery, distance or the flee timer.
        if(c.fleeBelowMilli>0&&!find(self.committed)){
            const MobCandidate* threat=nullptr;for(const auto& x:candidates)if(x.hostile&&distSq(x.position)<=c.fleeRange*c.fleeRange&&(!threat||distSq(x.position)<distSq(threat->position)||(distSq(x.position)==distSq(threat->position)&&x.id<threat->id)))threat=&x;
            bool start=s.mode!=MobMode::Flee&&self.hpMilli<=c.fleeBelowMilli&&threat;
            bool keep=s.mode==MobMode::Flee&&threat&&self.hpMilli<c.fleeRecoverMilli&&tick<s.until;
            if(start||keep){
                if(start){s.mode=MobMode::Flee;s.until=tick+c.fleeTicks;}
                s.target=threat->id;auto away=detail::normalOr(self.position-threat->position,areaForward(Fixed::fromRatio(self.id%360,360)));
                s.goal=self.position+away*c.fleeRange;
            }else if(s.mode==MobMode::Flee){s.mode=MobMode::Idle;s.target=0;s.until=tick+c.wanderDelay;}
        }
        if(s.mode!=MobMode::Flee){
            const MobCandidate* t=find(self.committed);
            if(!t)t=find(self.priority);
            if(!t){
                auto current=find(s.target);
                if(current&&s.mode==MobMode::Hunt&&distSq(current->position)<=c.loseRange*c.loseRange){
                    t=current;
                    if(nearest&&nearest!=current){auto keepWithin=current->position-self.position;auto margin=keepWithin.length()-c.switchMargin;
                        if(margin>Fixed{}&&distSq(nearest->position)<margin*margin)t=nearest;}
                }else t=nearest;
            }
            if(t){s.mode=MobMode::Hunt;s.target=t->id;s.goal=t->position;}
            else if(s.mode==MobMode::Hunt){s.mode=MobMode::Idle;s.target=0;s.until=tick+c.wanderDelay;}
        }
    }
    // Wander when nothing else to do: a reachable point in the [wanderMin, wanderMax] ring around the anchor.
    if((s.mode==MobMode::Idle||s.mode==MobMode::Wander)&&c.wander){
        bool arrived=s.mode==MobMode::Wander&&(distSq(s.goal)<=c.arriveRadius*c.arriveRadius||(s.goal-self.position).dot(s.goal-s.legFrom)<=Fixed{}||tick>=s.until);
        if(arrived){s.mode=MobMode::Idle;s.until=tick+c.wanderDelay;}
        if(s.mode==MobMode::Idle&&tick>=s.until){
            auto inner=c.wanderMin*c.wanderMin,outer=c.wanderMax*c.wanderMax;
            for(uint32_t i=0;i<c.wanderAttempts;++i){
                auto turn=rng.unit();auto radius=fxSqrt(inner+rng.unit()*(outer-inner));auto point=self.anchor+areaForward(turn)*radius;
                if(areaBounded(point)&&walkable(self.position,point)){s.mode=MobMode::Wander;s.goal=point;s.legFrom=self.position;s.until=tick+c.wanderLegTicks;break;}
            }
        }
    }else if(s.mode==MobMode::Idle)s.target=0;
    out.mode=s.mode;out.target=s.mode==MobMode::Hunt||s.mode==MobMode::Flee?s.target:0;out.goal=s.goal;out.switched=out.target!=previous&&out.target!=0;
    if(s.mode!=MobMode::Idle&&!self.movementLocked){
        auto to=s.goal-self.position;auto d=to.length();
        out.approach=s.mode!=MobMode::Hunt||d>c.stopDistance;
        if(out.approach&&d.raw>0)out.desired=to/d;
    }
    if(out.approach){out.correction=swarmCorrection(c,self,out.desired,neighbours);out.desired=detail::capLength(out.desired+out.correction,1_fx);}
    state=std::move(s);return out;
}
}
