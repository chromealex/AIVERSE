#pragma once
// Validated data for Ballistics.h / WeaponMods.h: generated-settings adapters (milli-units -> Fixed)
// and a JSON reader for material tables, weapon frames and attachments with path-qualified errors.
#include "../gameplay/Configuration.h"
#include "Ballistics.h"
#include "WeaponMods.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string>
#include <string_view>
namespace aiverse::lib::genre {
inline Fixed weaponMilli(int64_t value){return Fixed::fromRatio(value,1000);}
inline PenetrationConfig penetrationConfig(const gameplay::GameplaySettings& s){const auto& p=s.penetration;return {weaponMilli(p.powerMilli),weaponMilli(p.minDamageMilli),weaponMilli(p.minSpeedMilli),uint32_t(p.maxLayers)};}
inline ExplosionConfig explosionConfig(const gameplay::GameplaySettings& s){const auto& p=s.explosion;return {weaponMilli(p.innerRadiusMilli),weaponMilli(p.outerRadiusMilli),weaponMilli(p.damageMilli),weaponMilli(p.edgeScaleMilli),weaponMilli(p.impulseMilli),uint32_t(p.maxTargets)};}
// ammoFeed.kind: 0 magazine, 1 belt, 2 perShell, 3 energy.
inline FeedConfig feedConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.ammoFeed;
    return {FeedKind(uint8_t(p.kind)),uint32_t(p.capacity),uint32_t(p.ammoPerShot),uint32_t(p.intervalTicks),uint32_t(p.reloadTicks),uint32_t(p.emptyReloadTicks),uint32_t(p.reloadStartTicks),
        uint32_t(p.shellTicks),uint32_t(p.spinupTicks),uint32_t(p.heatPerShot),uint32_t(p.heatCapacity),uint32_t(p.coolPerTick),uint32_t(p.overheatTicks),uint32_t(p.recoverHeat),p.autoReload,p.interruptible};
}
// `tiers` evenly spaced thresholds from minTicks to maxTicks with scales from minScale to maxScale.
inline ChargeConfig chargeConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.charge;ChargeConfig c{uint32_t(p.minTicks),uint32_t(p.maxTicks),uint32_t(p.autoReleaseTicks),p.fireBelowMin,weaponMilli(p.minScaleMilli),{}};
    if(p.tiers==1)c.tiers.push_back({uint32_t(p.maxTicks),weaponMilli(p.maxScaleMilli)});
    for(int64_t i=0;p.tiers>1&&i<p.tiers;++i)c.tiers.push_back({uint32_t(p.minTicks+(p.maxTicks-p.minTicks)*i/(p.tiers-1)),weaponMilli(p.minScaleMilli+(p.maxScaleMilli-p.minScaleMilli)*i/(p.tiers-1))});
    return c;
}
inline AimConfig aimConfig(const gameplay::GameplaySettings& s){const auto& p=s.aimSolver;return {uint32_t(p.maxIterations),weaponMilli(p.toleranceMilli),p.preferHigh};}
// Cross-field rules the generated per-field ranges cannot express. Empty means every adapter is valid.
inline std::vector<std::string> weaponSettingsErrors(const gameplay::GameplaySettings& s){
    std::vector<std::string> e;const auto& f=s.ammoFeed;const auto& c=s.charge;
    bool magazine=f.kind==0||f.kind==2;
    if(magazine&&(f.capacity<1||f.ammoPerShot<1||f.ammoPerShot>f.capacity))e.push_back("ammoFeed.ammoPerShot: must be 1..capacity for magazine and perShell feeds");
    if(f.kind==2&&f.shellTicks<1)e.push_back("ammoFeed.shellTicks: perShell feeds need at least 1 tick per shell");
    if(f.kind==1&&f.ammoPerShot<1)e.push_back("ammoFeed.ammoPerShot: belt feeds draw at least 1 round per shot");
    if(f.heatPerShot&&f.recoverHeat>=f.heatCapacity)e.push_back("ammoFeed.recoverHeat: must be below heatCapacity when heatPerShot > 0");
    if(c.autoReleaseTicks&&c.autoReleaseTicks<std::max<int64_t>(c.minTicks,1))e.push_back("charge.autoReleaseTicks: must be 0 or at least minTicks");
    if(c.tiers>1&&c.maxTicks-c.minTicks<c.tiers-1)e.push_back("charge.tiers: maxTicks - minTicks must be at least tiers - 1");
    if(e.empty()&&(!valid(penetrationConfig(s))||!valid(explosionConfig(s))||!valid(feedConfig(s))||!valid(chargeConfig(s))||!valid(aimConfig(s))))e.push_back("weapon settings: invalid combination");
    return e;
}

struct WeaponDefinitions {MaterialTable materials;WeaponFrame frame;AttachmentSet attachments;};
namespace detail {
inline void weaponKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)found=found||n==key;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline int64_t weaponInteger(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    auto text=j.numberLiteral();int64_t value=0;auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": integer out of range "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline int64_t weaponOptional(const Json& j,const char* key,const std::string& path,int64_t low,int64_t high,int64_t def){return j.has(key)?weaponInteger(j[key],path+"."+key,low,high):def;}
inline const Json& weaponArray(const Json& j,const std::string& path,size_t limit){
    if(!j.isArray()||j.size()>limit)throw JsonError(path+": expected array of at most "+std::to_string(limit)+" entries");
    return j;
}
}
// {"materials":[{id,resistanceMilli,damageFalloffMilli,velocityFalloffMilli,minThicknessMilli,impenetrable}],
//  "frame":{"slots":[ids],"stats":[{id,baseMilli,minimumMilli,maximumMilli}]},
//  "attachments":[{id,slot,modifiers:[{stat,op:"add"|"mul",valueMilli}]}]}. All sections optional;
// attachments require a frame. Throws JsonError("weapons.<path>: ...") on the first problem.
inline WeaponDefinitions readWeaponDefinitions(const Json& j){
    using namespace detail;WeaponDefinitions d;const std::string root="weapons";constexpr int64_t big=1000000000000;
    weaponKeys(j,root,{"materials","frame","attachments"});
    if(j.has("materials"))for(size_t i=0;const auto& m:weaponArray(j["materials"],root+".materials",4096).items()){
        auto path=root+".materials["+std::to_string(i++)+"]";
        weaponKeys(m,path,{"id","resistanceMilli","damageFalloffMilli","velocityFalloffMilli","minThicknessMilli","impenetrable"});
        PenetrationMaterial mat{weaponInteger(m["id"],path+".id",1,INT64_MAX),weaponMilli(weaponOptional(m,"resistanceMilli",path,0,1000000000,1000)),
            weaponMilli(weaponOptional(m,"damageFalloffMilli",path,0,1000000,0)),weaponMilli(weaponOptional(m,"velocityFalloffMilli",path,0,1000000,0)),
            weaponMilli(weaponOptional(m,"minThicknessMilli",path,0,1000000,0)),false};
        if(m.has("impenetrable")){if(m["impenetrable"].type()!=Json::Type::Bool)throw JsonError(path+".impenetrable: expected boolean");mat.impenetrable=m["impenetrable"].boolean();}
        if(!d.materials.emplace(mat.id,mat).second)throw JsonError(path+".id: duplicate material id "+std::to_string(mat.id));
    }
    if(j.has("frame")){
        const auto& f=j["frame"];auto path=root+".frame";weaponKeys(f,path,{"slots","stats"});
        for(size_t i=0;const auto& slot:weaponArray(f["slots"],path+".slots",64).items())d.frame.slots.push_back(weaponInteger(slot,path+".slots["+std::to_string(i++)+"]",1,INT64_MAX));
        std::sort(d.frame.slots.begin(),d.frame.slots.end());
        if(std::adjacent_find(d.frame.slots.begin(),d.frame.slots.end())!=d.frame.slots.end())throw JsonError(path+".slots: duplicate slot id");
        for(size_t i=0;const auto& st:weaponArray(f["stats"],path+".stats",1024).items()){
            auto at=path+".stats["+std::to_string(i++)+"]";weaponKeys(st,at,{"id","baseMilli","minimumMilli","maximumMilli"});
            auto id=weaponInteger(st["id"],at+".id",1,INT64_MAX);auto base=weaponInteger(st["baseMilli"],at+".baseMilli",-big,big);
            auto lo=weaponOptional(st,"minimumMilli",at,-big,big,std::min<int64_t>(base,0)),hi=weaponOptional(st,"maximumMilli",at,-big,big,big);
            if(lo>base||base>hi)throw JsonError(at+": requires minimumMilli <= baseMilli <= maximumMilli");
            if(!d.frame.stats.emplace(id,StatLimit{weaponMilli(base),weaponMilli(lo),weaponMilli(hi)}).second)throw JsonError(at+".id: duplicate stat id "+std::to_string(id));
        }
        if(d.frame.stats.empty())throw JsonError(path+".stats: at least one stat is required");
    }
    if(j.has("attachments")){
        if(!j.has("frame"))throw JsonError(root+".attachments: requires a frame");
        for(size_t i=0;const auto& a:weaponArray(j["attachments"],root+".attachments",4096).items()){
            auto path=root+".attachments["+std::to_string(i++)+"]";weaponKeys(a,path,{"id","slot","modifiers"});
            AttachmentDef def{weaponInteger(a["id"],path+".id",1,INT64_MAX),weaponInteger(a["slot"],path+".slot",1,INT64_MAX),{}};
            if(!std::binary_search(d.frame.slots.begin(),d.frame.slots.end(),def.slot))throw JsonError(path+".slot: unknown frame slot "+std::to_string(def.slot));
            if(a.has("modifiers"))for(size_t k=0;const auto& m:weaponArray(a["modifiers"],path+".modifiers",64).items()){
                auto at=path+".modifiers["+std::to_string(k++)+"]";weaponKeys(m,at,{"stat","op","valueMilli"});
                StatModifier mod{weaponInteger(m["stat"],at+".stat",1,INT64_MAX),StatOp::Add,{}};
                if(!d.frame.stats.contains(mod.stat))throw JsonError(at+".stat: unknown frame stat "+std::to_string(mod.stat));
                auto op=m["op"].str();if(op=="mul")mod.op=StatOp::Mul;else if(op!="add")throw JsonError(at+".op: expected \"add\" or \"mul\"");
                mod.value=mod.op==StatOp::Add?weaponMilli(weaponInteger(m["valueMilli"],at+".valueMilli",-big,big)):weaponMilli(weaponInteger(m["valueMilli"],at+".valueMilli",0,1000000));
                def.modifiers.push_back(mod);
            }
            if(!d.attachments.emplace(def.id,std::move(def)).second)throw JsonError(path+".id: duplicate attachment id");
        }
    }
    if(!valid(d.materials)||(j.has("frame")&&!valid(d.frame,d.attachments)))throw JsonError(root+": invalid definitions");
    return d;
}
}
