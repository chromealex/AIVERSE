#pragma once
// physical-interactions: breakable objects. Health with per damage type scale (milli, 0 = immune) and
// threshold (hits whose scaled damage is below it are ignored), ordered damage stages reached when health
// drops to a stage's health, and debris spawn requests as data (kind, count, position, seed). Damage events
// carry ids strictly increasing per object; a repeated or older id is ignored, so delayed duplicates never
// apply twice. No presentation: a Box3D world may simulate the requested debris as non-authoritative visuals.
#include "InteractionConfig.h"
#include "engine/core/Hash.h"
namespace aiverse::lib::genre {
struct DamageRule {int64_t type=0,scaleMilli=1000,threshold=0;};
struct DebrisSpec {int64_t kind=0;uint32_t count=1;};
struct BreakStage {int64_t stage=0,health=0;std::vector<DebrisSpec> debris;}; // reached when health <= this.health
// damage sorted by type; stages: ascending stage id with strictly decreasing health in (0, health).
struct BreakableDef {int64_t id=0,health=100,defaultScaleMilli=1000;std::vector<DamageRule> damage;std::vector<BreakStage> stages;std::vector<DebrisSpec> brokenDebris;};
using BreakableDefs=std::map<int64_t,BreakableDef>;
inline bool valid(const DebrisSpec& x){return x.kind>0&&x.count>=1&&x.count<=1000;}
inline bool valid(const BreakableDef& d){
    if(d.id<=0||d.health<1||d.health>kPhysMaxHealth||d.defaultScaleMilli<0||d.defaultScaleMilli>1000000)return false;
    if(d.damage.size()>kPhysMaxRules||d.stages.size()>kPhysMaxStages||d.brokenDebris.size()>kPhysMaxDebris)return false;
    for(std::size_t i=0;i<d.damage.size();++i){const auto& r=d.damage[i];if(r.type<=0||(i&&d.damage[i-1].type>=r.type)||r.scaleMilli<0||r.scaleMilli>1000000||r.threshold<0||r.threshold>kPhysMaxHealth)return false;}
    for(std::size_t i=0;i<d.stages.size();++i){const auto& s=d.stages[i];
        if(s.stage<=0||s.health<=0||s.health>=d.health||(i&&(d.stages[i-1].stage>=s.stage||d.stages[i-1].health<=s.health))||s.debris.size()>kPhysMaxDebris)return false;
        for(const auto& x:s.debris)if(!valid(x))return false;}
    for(const auto& x:d.brokenDebris)if(!valid(x))return false;
    return true;
}
inline bool valid(const BreakableDefs& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}

struct BreakableInstance {int64_t health=0,stage=0,lastDamage=0;bool broken=false;};
struct BreakableState {std::map<int64_t,BreakableInstance> objects;uint32_t lastTick=0;bool initialized=false;};
struct DamageInput {int64_t id=0,object=0,type=0,amount=0,source=0;Vec3 position{};};
// stage 0 = final break debris. seed: deterministic per (object, stage, damage id) for presentation scatter.
struct DebrisRequest {int64_t object=0,stage=0,kind=0;uint32_t count=0;Vec3 position{};uint64_t seed=0;};
struct BreakableReport {std::vector<InteractionEvent> events;std::vector<DebrisRequest> debris;};

inline bool addBreakable(BreakableState& s,const BreakableDef& d){return valid(d)&&s.objects.size()<kPhysMaxObjects&&s.objects.emplace(d.id,BreakableInstance{d.health,0,0,false}).second;}
inline bool valid(const BreakableState& s,const BreakableDefs& defs){
    if(s.objects.size()>kPhysMaxObjects)return false;
    for(const auto& [id,x]:s.objects){auto d=defs.find(id);if(d==defs.end()||x.health<0||x.health>d->second.health||x.stage<0||x.lastDamage<0||x.broken!=(x.health==0))return false;}
    return true;
}
inline int64_t breakableScaled(const BreakableDef& d,const DamageInput& x){
    int64_t scale=d.defaultScaleMilli;auto it=std::lower_bound(d.damage.begin(),d.damage.end(),x.type,[](const DamageRule& r,int64_t t){return r.type<t;});
    if(it!=d.damage.end()&&it->type==x.type)scale=it->scaleMilli;
    return x.amount*scale/1000; // amount <= 1e12, scale <= 1e6: fits int64
}
inline int64_t breakableThreshold(const BreakableDef& d,int64_t type){
    auto it=std::lower_bound(d.damage.begin(),d.damage.end(),type,[](const DamageRule& r,int64_t t){return r.type<t;});return it!=d.damage.end()&&it->type==type?it->threshold:0;
}

// One tick: damage in ascending (object, id). Events: Damaged (value = applied), DamageIgnored (value = reason),
// StageReached (value = stage id), Broken. Unknown objects, non-positive ids/types or negative amounts reject the tick.
inline std::optional<BreakableReport> stepBreakables(const BreakableDefs& defs,BreakableState& state,uint32_t tick,std::span<const DamageInput> damage){
    using K=InteractionEventKind;
    if(!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs)||damage.size()>kPhysMaxCommands)return std::nullopt;
    for(const auto& x:damage)if(x.id<=0||x.type<=0||x.amount<0||x.amount>kPhysMaxHealth||!state.objects.contains(x.object)||!physBounded(x.position))return std::nullopt;
    std::vector<DamageInput> sorted(damage.begin(),damage.end());
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.object!=b.object?a.object<b.object:a.id<b.id;});
    auto s=state;s.initialized=true;s.lastTick=tick;BreakableReport report;auto& ev=report.events;
    auto spawn=[&](int64_t object,int64_t stage,const std::vector<DebrisSpec>& list,const DamageInput& x){
        for(const auto& spec:list){Hasher h;h.i64(object);h.i64(stage);h.i64(x.id);h.i64(spec.kind);report.debris.push_back({object,stage,spec.kind,spec.count,x.position,h.value()});}
    };
    for(const auto& x:sorted){
        const auto& d=defs.at(x.object);auto& o=s.objects.at(x.object);
        auto ignore=[&](int64_t why){ev.push_back({K::DamageIgnored,x.object,x.source,why,0,x.position});};
        if(o.broken){ignore(ReasonAlreadyBroken);continue;}
        if(x.id<=o.lastDamage){ignore(ReasonDuplicate);continue;}
        o.lastDamage=x.id;auto amount=breakableScaled(d,x);
        if(amount<=0){ignore(ReasonImmune);continue;}
        if(amount<breakableThreshold(d,x.type)){ignore(ReasonBelowThreshold);continue;}
        auto applied=std::min(amount,o.health);o.health-=applied;ev.push_back({K::Damaged,x.object,x.source,applied,0,x.position});
        for(const auto& st:d.stages)if(st.stage>o.stage&&o.health<=st.health){o.stage=st.stage;ev.push_back({K::StageReached,x.object,x.source,st.stage,0,x.position});spawn(x.object,st.stage,st.debris,x);}
        if(!o.health){o.broken=true;ev.push_back({K::Broken,x.object,x.source,0,0,x.position});spawn(x.object,0,d.brokenDebris,x);}
    }
    state=std::move(s);return report;
}
}
