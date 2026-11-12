#pragma once
// JSON encounter data (boss-encounters). Integers only: lengths in milli-metres ("radiusMilli"), angles in
// milli-turns ("halfAngleMilli": 167 = 60 degrees each side), times in ticks, multipliers in milli (1000 = x1).
// {"ticksPerSecond":20,"statuses":[...],"attacks":[...],"bosses":[...],"schedule":{...},"mob":{...}}; see
// examples/libraries/genre/boss-encounter.json and docs/genre-packages.md (boss-encounters). Errors name the path.
#include "BossEncounter.h"
#include "BossSchedule.h"
#include "CombatData.h"
#include "MobBehaviour.h"
#include "StatusEffects.h"
#include "Telegraphs.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
struct EncounterData {uint32_t ticksPerSecond=20;StatusSet statuses;TelegraphSet attacks;BossSet bosses;ScheduleDef schedule;MobBehaviourConfig mob;};
namespace detail {
inline int64_t encInt(const Json& j,const std::string& key,const std::string& path,int64_t low,int64_t high,int64_t def){
    if(!j.has(key))return def;
    return hitboxInteger(j[key],path+"."+key,low,high);
}
inline Fixed encMilli(const Json& j,const std::string& key,const std::string& path,int64_t low,int64_t high,Fixed def){
    if(!j.has(key))return def;
    return Fixed::fromRatio(hitboxInteger(j[key],path+"."+key,low,high),1000);
}
inline bool encBool(const Json& j,const std::string& key,const std::string& path,bool def){
    if(!j.has(key))return def;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected true or false");
    return j[key].boolean();
}
template<class E> E encEnum(const Json& j,const std::string& key,const std::string& path,std::initializer_list<std::pair<const char*,E>> names,E def){
    if(!j.has(key))return def;
    auto v=j[key].str();
    std::string list;
    for(auto [n,e]:names){if(v==n)return e;list+=(list.empty()?"":", ")+std::string(n);}
    throw JsonError(path+"."+key+": expected "+list);
}
inline std::vector<int64_t> encIds(const Json& j,const std::string& key,const std::string& path,std::size_t max){
    std::vector<int64_t> out;if(!j.has(key))return out;const auto& a=j[key];
    if(!a.isArray()||a.size()>max)throw JsonError(path+"."+key+": expected an array of at most "+std::to_string(max)+" ids");
    for(std::size_t i=0;i<a.size();++i)out.push_back(hitboxInteger(a[i],path+"."+key+"["+std::to_string(i)+"]",1,INT64_MAX));
    return out;
}
inline const Json& encArray(const Json& j,const std::string& key,const std::string& path,std::size_t max){
    const auto& a=j[key];if(!a.isArray()||a.size()>max)throw JsonError(path+"."+key+": expected an array of at most "+std::to_string(max)+" entries");return a;
}
constexpr int64_t kMm=100000000;   // 100 km in milli-metres
}
inline StatusSet readStatusEffects(const Json& list){
    using namespace detail;StatusSet out;if(list.isNull())return out;
    if(!list.isArray()||list.size()>4096)throw JsonError("statuses: expected array");
    for(const auto& j:list.items()){
        hitboxKeys(j,"statuses[]",{"id","kind","duration","interval","maxStacks","speedMilli","damagePerTick","stacking","anchor"});
        StatusEffectDef d;d.id=hitboxInteger(j["id"],"statuses[].id",1,INT64_MAX);auto path="status "+std::to_string(d.id);
        if(out.contains(d.id))throw JsonError(path+": duplicate id");
        d.kind=encEnum<StatusKind>(j,"kind",path,{{"slow",StatusKind::Slow},{"root",StatusKind::Root},{"stun",StatusKind::Stun},{"burn",StatusKind::Burn}},StatusKind::Slow);
        if(!j.has("kind"))throw JsonError(path+".kind: expected slow, root, stun or burn");
        d.stacking=encEnum<StatusStacking>(j,"stacking",path,{{"independent",StatusStacking::Independent},{"refresh",StatusStacking::Refresh},{"reject",StatusStacking::Reject}},
            d.kind==StatusKind::Burn?StatusStacking::Independent:d.kind==StatusKind::Stun?StatusStacking::Reject:StatusStacking::Refresh);
        d.anchor=encEnum<StatusAnchor>(j,"anchor",path,{{"ground",StatusAnchor::Ground},{"modelTop",StatusAnchor::ModelTop}},d.kind==StatusKind::Stun?StatusAnchor::ModelTop:StatusAnchor::Ground);
        d.duration=uint32_t(hitboxInteger(j["duration"],path+".duration",1,360000));
        d.tickInterval=uint32_t(encInt(j,"interval",path,1,36000,10));d.maxStacks=uint32_t(encInt(j,"maxStacks",path,1,64,d.stacking==StatusStacking::Independent?8:1));
        d.speedMilli=encInt(j,"speedMilli",path,0,1000,d.kind==StatusKind::Slow?500:1000);d.damagePerTick=encInt(j,"damagePerTick",path,0,1000000000,0);
        if(d.kind!=StatusKind::Burn&&d.damagePerTick)throw JsonError(path+".damagePerTick: only burn deals damage");
        if(!valid(d))throw JsonError(path+": invalid status");
        out[d.id]=d;
    }
    return out;
}
inline TelegraphSet readTelegraphs(const Json& list,const StatusSet& statuses){
    using namespace detail;TelegraphSet out;
    if(!list.isArray()||list.size()==0||list.size()>4096)throw JsonError("attacks: expected a non-empty array");
    for(const auto& j:list.items()){
        hitboxKeys(j,"attacks[]",{"id","shape","radiusMilli","innerMilli","halfAngleMilli","lengthMilli","halfWidthMilli","anchor","offsetMilli","windup","active","recovery",
            "interval","cooldown","damage","damageMin","falloff","oncePerTarget","trackAim","sweep","cancelOnSourceDeath","friendlyFire","minRangeMilli","maxRangeMilli","statuses"});
        TelegraphDef d;d.id=hitboxInteger(j["id"],"attacks[].id",1,INT64_MAX);auto path="attack "+std::to_string(d.id);
        if(out.contains(d.id))throw JsonError(path+": duplicate id");
        d.area.shape=encEnum<AreaShape>(j,"shape",path,{{"circle",AreaShape::Circle},{"cone",AreaShape::Cone},{"line",AreaShape::Line},{"ring",AreaShape::Ring}},AreaShape::Circle);
        if(!j.has("shape"))throw JsonError(path+".shape: expected circle, cone, line or ring");
        auto needs=[&](const char* key,bool required){if(required!=j.has(key))throw JsonError(path+"."+key+(required?": required for this shape":": not used by this shape"));};
        auto shape=d.area.shape;
        needs("radiusMilli",shape!=AreaShape::Line);needs("lengthMilli",shape==AreaShape::Line);needs("halfWidthMilli",shape==AreaShape::Line);
        needs("halfAngleMilli",shape==AreaShape::Cone);needs("innerMilli",shape==AreaShape::Ring);
        d.area.radius=encMilli(j,"radiusMilli",path,1,1000000,d.area.radius);d.area.inner=encMilli(j,"innerMilli",path,0,1000000,{});
        d.area.halfAngle=encMilli(j,"halfAngleMilli",path,1,500,d.area.halfAngle);d.area.length=encMilli(j,"lengthMilli",path,1,1000000,d.area.length);
        d.area.halfWidth=encMilli(j,"halfWidthMilli",path,1,1000000,d.area.halfWidth);
        d.anchor=encEnum<AreaAnchor>(j,"anchor",path,{{"source",AreaAnchor::Source},{"aim",AreaAnchor::Aim}},AreaAnchor::Source);
        if(j.has("offsetMilli")){const auto& o=j["offsetMilli"];if(!o.isArray()||o.size()!=2)throw JsonError(path+".offsetMilli: expected [right, forward]");
            d.offset={Fixed::fromRatio(hitboxInteger(o[0],path+".offsetMilli[0]",-kMm,kMm),1000),Fixed::fromRatio(hitboxInteger(o[1],path+".offsetMilli[1]",-kMm,kMm),1000)};}
        d.windup=uint32_t(encInt(j,"windup",path,0,36000,d.windup));d.active=uint32_t(encInt(j,"active",path,1,36000,d.active));d.recovery=uint32_t(encInt(j,"recovery",path,0,36000,d.recovery));
        d.interval=uint32_t(encInt(j,"interval",path,0,36000,0));d.cooldown=uint32_t(encInt(j,"cooldown",path,0,36000,0));
        d.damage=encInt(j,"damage",path,0,1000000000,0);d.damageMin=encInt(j,"damageMin",path,0,1000000000,d.damage);
        d.falloff=encBool(j,"falloff",path,false);d.oncePerTarget=encBool(j,"oncePerTarget",path,true);d.trackAim=encBool(j,"trackAim",path,false);
        d.sweep=encBool(j,"sweep",path,false);d.cancelOnSourceDeath=encBool(j,"cancelOnSourceDeath",path,true);d.friendlyFire=encBool(j,"friendlyFire",path,false);
        d.minRange=encMilli(j,"minRangeMilli",path,0,kMm,{});d.maxRange=encMilli(j,"maxRangeMilli",path,0,kMm,{});
        d.statuses=encIds(j,"statuses",path,kTelegraphMaxStatuses);
        for(auto s:d.statuses)if(!statuses.contains(s))throw JsonError(path+".statuses: unknown status "+std::to_string(s));
        if(d.interval>d.active)throw JsonError(path+".interval: must not exceed active");
        if(d.damageMin>d.damage)throw JsonError(path+".damageMin: must not exceed damage");
        if(d.sweep&&shape!=AreaShape::Line)throw JsonError(path+".sweep: only line attacks sweep");
        if(!valid(d))throw JsonError(path+": invalid attack");
        out[d.id]=std::move(d);
    }
    return out;
}
inline BossSet readBosses(const Json& list,const TelegraphSet& attacks){
    using namespace detail;BossSet out;if(list.isNull())return out;
    if(!list.isArray()||list.size()>1024)throw JsonError("bosses: expected array");
    for(const auto& j:list.items()){
        hitboxKeys(j,"bosses[]",{"id","archetype","phases","enrage"});
        BossDef b;b.id=hitboxInteger(j["id"],"bosses[].id",1,INT64_MAX);auto path="boss "+std::to_string(b.id);
        if(out.contains(b.id))throw JsonError(path+": duplicate id");
        b.archetype=uint8_t(encInt(j,"archetype",path,0,8,0));
        const auto& phases=encArray(j,"phases",path,kBossMaxPhases);if(phases.size()==0)throw JsonError(path+".phases: at least one phase");
        for(std::size_t i=0;i<phases.size();++i){
            const auto& pj=phases[i];
            hitboxKeys(pj,path+".phases[]",{"id","hpBelowMilli","afterTicks","damageMilli","speedMilli","cooldownMilli","globalCooldown","transitionTicks","mode","pattern","adds"});
            BossPhaseDef p;p.id=hitboxInteger(pj["id"],path+".phases[].id",1,INT64_MAX);auto pp=path+".phase "+std::to_string(p.id);
            p.hpBelowMilli=encInt(pj,"hpBelowMilli",pp,0,1000,1000);p.afterTicks=uint32_t(encInt(pj,"afterTicks",pp,0,3600000,0));
            p.damageMilli=encInt(pj,"damageMilli",pp,0,100000,1000);p.speedMilli=encInt(pj,"speedMilli",pp,0,100000,1000);p.cooldownMilli=encInt(pj,"cooldownMilli",pp,0,100000,1000);
            p.globalCooldown=uint32_t(encInt(pj,"globalCooldown",pp,0,36000,0));p.transitionTicks=uint32_t(encInt(pj,"transitionTicks",pp,0,36000,0));
            p.mode=encEnum<PatternMode>(pj,"mode",pp,{{"byRange",PatternMode::ByRange},{"sequence",PatternMode::Sequence},{"weighted",PatternMode::Weighted}},PatternMode::ByRange);
            if(i>0&&p.hpBelowMilli>=1000&&p.afterTicks==0)throw JsonError(pp+": needs hpBelowMilli < 1000 or afterTicks");
            if(pj.has("pattern"))for(const auto& e:encArray(pj,"pattern",pp,kBossMaxPattern).items()){
                hitboxKeys(e,pp+".pattern[]",{"attack","alternate","minRangeMilli","maxRangeMilli","weight"});
                BossPatternEntry x;x.attack=hitboxInteger(e["attack"],pp+".pattern[].attack",1,INT64_MAX);x.alternate=encInt(e,"alternate",pp,1,INT64_MAX,0);
                if(!attacks.contains(x.attack))throw JsonError(pp+".pattern: unknown attack "+std::to_string(x.attack));
                if(x.alternate&&!attacks.contains(x.alternate))throw JsonError(pp+".pattern: unknown attack "+std::to_string(x.alternate));
                x.minRange=encMilli(e,"minRangeMilli",pp,0,kMm,{});x.maxRange=encMilli(e,"maxRangeMilli",pp,0,kMm,{});x.weight=uint32_t(encInt(e,"weight",pp,1,1000000,1));
                p.pattern.push_back(x);
            }
            if(pj.has("adds"))for(const auto& a:encArray(pj,"adds",pp,kBossMaxAdds).items()){
                hitboxKeys(a,pp+".adds[]",{"unit","count","maxAlive","every","minRadiusMilli","maxRadiusMilli"});
                AddWaveDef w;w.unit=hitboxInteger(a["unit"],pp+".adds[].unit",1,INT64_MAX);w.count=uint32_t(encInt(a,"count",pp,1,256,1));
                w.maxAlive=uint32_t(encInt(a,"maxAlive",pp,0,4096,0));w.every=uint32_t(encInt(a,"every",pp,0,3600000,0));
                w.minRadius=encMilli(a,"minRadiusMilli",pp,0,1000000,w.minRadius);w.maxRadius=encMilli(a,"maxRadiusMilli",pp,0,1000000,w.maxRadius);
                if(w.maxRadius<w.minRadius)throw JsonError(pp+".adds: maxRadiusMilli below minRadiusMilli");
                p.adds.push_back(w);
            }
            for(const auto& q:b.phases)if(q.id==p.id)throw JsonError(pp+": duplicate phase id");
            b.phases.push_back(std::move(p));
        }
        if(j.has("enrage")){
            const auto& e=j["enrage"];hitboxKeys(e,path+".enrage",{"afterTicks","hpBelowMilli","damageMilli","speedMilli","cooldownMilli"});auto ep=path+".enrage";
            b.enrage.afterTicks=uint32_t(encInt(e,"afterTicks",ep,0,3600000,0));b.enrage.hpBelowMilli=encInt(e,"hpBelowMilli",ep,0,1000,0);
            b.enrage.damageMilli=encInt(e,"damageMilli",ep,0,100000,1000);b.enrage.speedMilli=encInt(e,"speedMilli",ep,0,100000,1000);b.enrage.cooldownMilli=encInt(e,"cooldownMilli",ep,0,100000,1000);
        }
        if(!valid(b,attacks))throw JsonError(path+": invalid boss");
        out[b.id]=std::move(b);
    }
    return out;
}
inline ScheduleDef readSchedule(const Json& j,const BossSet& bosses){
    using namespace detail;ScheduleDef d;if(j.isNull())return d;
    hitboxKeys(j,"schedule",{"personal","rotation","archetypes","windows"});
    d.personal=encBool(j,"personal","schedule",true);d.rotation=encBool(j,"rotation","schedule",false);d.archetypes=uint8_t(encInt(j,"archetypes","schedule",0,8,0));
    if(d.rotation&&d.archetypes==0)throw JsonError("schedule.archetypes: rotation needs 1..8 archetypes");
    if(j.has("windows"))for(const auto& w:encArray(j,"windows","schedule",kScheduleMaxWindows).items()){
        hitboxKeys(w,"schedule.windows[]",{"id","start","end","breather","variants","excludePrevious","pauseWaves"});
        EncounterWindowDef x;x.id=hitboxInteger(w["id"],"schedule.windows[].id",1,INT64_MAX);auto path="schedule.window "+std::to_string(x.id);
        x.start=uint32_t(hitboxInteger(w["start"],path+".start",0,3600000));x.end=uint32_t(hitboxInteger(w["end"],path+".end",1,3600000));
        if(x.end<=x.start)throw JsonError(path+".end: must be after start");
        if(!d.windows.empty()&&x.start<d.windows.back().start)throw JsonError(path+".start: windows must be in start order");
        x.breather=uint32_t(encInt(w,"breather",path,0,3600000,0));x.excludePrevious=encBool(w,"excludePrevious",path,false);x.pauseWaves=encBool(w,"pauseWaves",path,true);
        for(const auto& v:encArray(w,"variants",path,kScheduleMaxVariants).items()){
            hitboxKeys(v,path+".variants[]",{"boss","weight","archetype"});
            BossVariant b;b.boss=hitboxInteger(v["boss"],path+".variants[].boss",1,INT64_MAX);
            auto known=bosses.find(b.boss);if(!bosses.empty()&&known==bosses.end())throw JsonError(path+".variants: unknown boss "+std::to_string(b.boss));
            b.weight=uint32_t(encInt(v,"weight",path,1,1000000,1));b.archetype=uint8_t(encInt(v,"archetype",path,0,8,known!=bosses.end()?known->second.archetype:0));
            if(d.rotation&&b.archetype>d.archetypes)throw JsonError(path+".variants: archetype above schedule.archetypes");
            x.variants.push_back(b);
        }
        if(x.variants.empty())throw JsonError(path+".variants: at least one boss");
        for(const auto& o:d.windows)if(o.id==x.id)throw JsonError(path+": duplicate window id");
        d.windows.push_back(std::move(x));
    }
    if(!valid(d))throw JsonError("schedule: invalid schedule");
    return d;
}
inline MobBehaviourConfig readMobBehaviour(const Json& j){
    using namespace detail;MobBehaviourConfig c;if(j.isNull())return c;const std::string p="mob";
    hitboxKeys(j,p,{"aggroRangeMilli","loseRangeMilli","stopDistanceMilli","switchMarginMilli","wanderMinMilli","wanderMaxMilli","arriveRadiusMilli","leashRadiusMilli",
        "wanderDelay","wanderLegTicks","wanderAttempts","fleeBelowMilli","fleeRecoverMilli","fleeRangeMilli","fleeTicks","wander","spacingMilli","correctionMilli","lookAheadMilli","maxNeighbours"});
    c.aggroRange=encMilli(j,"aggroRangeMilli",p,0,kMm,c.aggroRange);c.loseRange=encMilli(j,"loseRangeMilli",p,0,kMm,fxMax(c.loseRange,c.aggroRange));
    c.stopDistance=encMilli(j,"stopDistanceMilli",p,0,kMm,c.stopDistance);c.switchMargin=encMilli(j,"switchMarginMilli",p,0,kMm,c.switchMargin);
    c.wanderMin=encMilli(j,"wanderMinMilli",p,0,kMm,c.wanderMin);c.wanderMax=encMilli(j,"wanderMaxMilli",p,0,kMm,fxMax(c.wanderMax,c.wanderMin));
    c.arriveRadius=encMilli(j,"arriveRadiusMilli",p,1,kMm,c.arriveRadius);c.leashRadius=encMilli(j,"leashRadiusMilli",p,0,kMm,{});
    c.wanderDelay=uint32_t(encInt(j,"wanderDelay",p,0,36000,c.wanderDelay));c.wanderLegTicks=uint32_t(encInt(j,"wanderLegTicks",p,1,36000,c.wanderLegTicks));
    c.wanderAttempts=uint32_t(encInt(j,"wanderAttempts",p,1,32,c.wanderAttempts));c.fleeBelowMilli=encInt(j,"fleeBelowMilli",p,0,1000,0);
    c.fleeRecoverMilli=encInt(j,"fleeRecoverMilli",p,0,1000,c.fleeBelowMilli);c.fleeRange=encMilli(j,"fleeRangeMilli",p,0,kMm,c.fleeRange);
    c.fleeTicks=uint32_t(encInt(j,"fleeTicks",p,0,36000,c.fleeTicks));c.wander=encBool(j,"wander",p,true);
    c.spacingMilli=encInt(j,"spacingMilli",p,1,10000,c.spacingMilli);c.correctionMilli=encInt(j,"correctionMilli",p,0,1000,c.correctionMilli);
    c.lookAhead=encMilli(j,"lookAheadMilli",p,0,kMm,c.lookAhead);c.maxNeighbours=uint32_t(encInt(j,"maxNeighbours",p,0,int64_t(kMobMaxNeighbours),c.maxNeighbours));
    if(c.loseRange<c.aggroRange)throw JsonError("mob.loseRangeMilli: must be at least aggroRangeMilli");
    if(c.wanderMax<c.wanderMin)throw JsonError("mob.wanderMaxMilli: must be at least wanderMinMilli");
    if(c.fleeRecoverMilli<c.fleeBelowMilli)throw JsonError("mob.fleeRecoverMilli: must be at least fleeBelowMilli");
    if(!valid(c))throw JsonError("mob: invalid behaviour");
    return c;
}
inline EncounterData readEncounterData(const Json& j){
    detail::hitboxKeys(j,"encounter",{"ticksPerSecond","statuses","attacks","bosses","schedule","mob"});
    EncounterData d;d.ticksPerSecond=uint32_t(detail::encInt(j,"ticksPerSecond","encounter",1,1000,20));
    d.statuses=readStatusEffects(j["statuses"]);d.attacks=readTelegraphs(j["attacks"],d.statuses);d.bosses=readBosses(j["bosses"],d.attacks);
    d.schedule=readSchedule(j["schedule"],d.bosses);d.mob=readMobBehaviour(j["mob"]);return d;
}
// Definition hash to bind in save envelopes: the canonical compact dump of the source document.
inline uint64_t encounterDataHash(const Json& source){Hasher h;h.str(source.dump(-1));return h.value();}
}
