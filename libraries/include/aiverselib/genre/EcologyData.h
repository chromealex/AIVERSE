#pragma once
// JSON for survival-ecology (example: examples/libraries/genre/production-survival/ecology.json):
// {"ecology":{"periodTicks":1200,"noiseMilli":0,
//    "species":[{"id":1,"growthMilli":250,"mortalityMilli":10},
//               {"id":2,"growthMilli":40,"mortalityMilli":30,"foodNeedMilli":800,"starvationMilli":150,"migrateMilli":200,
//                "diet":[{"prey":1,"attackMilli":900,"halfSaturation":20,"conversionMilli":150}]}],
//    "regions":[{"id":1,"capacity":[[1,120],[2,8]],"neighbors":[2]}]},
//  "animals":{"ticksPerHour":50,"bounds":[[-200000,-200000],[200000,200000]],
//    "species":[{"species":1,"fears":[0,2],"walkMilli":40,"runMilli":220,"fleeMilli":15000,"grazeGain":6,"sleepFrom":22,"sleepTo":5}]},
//  "resources":[{"id":1,"resource":1,"region":1,"amount":40,"capacity":40,"regrowPerPeriod":4,"periodTicks":600,"regrowDelayTicks":1200}]}
// Distances/speeds are milli-metres (per tick for speeds). Errors: JsonError("ecology.<path>: ...") / "animals." / "resources".
#include "Ecology.h"
#include "EcologyAnimals.h"
#include "EcologyResources.h"
#include "SettlementJson.h"
namespace aiverse::lib::genre {
namespace detail_ecology_data {
using namespace detail_settlement_json;
inline constexpr std::array<std::pair<std::string_view,Awareness>,4> kAwareness{{{"unaware",Awareness::Unaware},{"suspicious",Awareness::Suspicious},{"searching",Awareness::Searching},{"alert",Awareness::Alert}}};
inline Fixed metres(const Json& j,const char* key,const std::string& path,Fixed fallback){return j.has(key)?Fixed::fromRatio(integer(j[key],path+"."+key,0,1000000),1000):fallback;}
inline Vec2 point(const Json& j,const std::string& path){
    if(!j.isArray()||j.size()!=2)throw JsonError(path+": expected [x, z] in milli-metres");
    return {Fixed::fromRatio(integer(j[0],path+"[0]",-1000000000,1000000000),1000),Fixed::fromRatio(integer(j[1],path+"[1]",-1000000000,1000000000),1000)};
}
}
inline EcologyDefs readEcologyDefinitions(const Json& j){
    using namespace detail_ecology_data;const std::string root="ecology";
    keys(j,root,{"periodTicks","noiseMilli","species","regions"});EcologyDefs d;
    d.periodTicks=optional(j,"periodTicks",root,1,100000000,60);d.noiseMilli=optional(j,"noiseMilli",root,0,1000,0);
    const auto& species=list(j,"species",root,kEcologyMaxDefs);
    for(std::size_t i=0;i<species.size();++i){
        const auto p=at(root+".species",i);const auto& s=species[i];keys(s,p,{"id","growthMilli","mortalityMilli","foodNeedMilli","starvationMilli","migrateMilli","diet"});
        EcologySpeciesDef def;def.id=required(s,"id",p,1,INT64_MAX);def.growthMilli=optional(s,"growthMilli",p,0,100000,0);def.mortalityMilli=optional(s,"mortalityMilli",p,0,100000,0);
        def.foodNeedMilli=optional(s,"foodNeedMilli",p,0,100000,0);def.starvationMilli=optional(s,"starvationMilli",p,0,100000,0);def.migrateMilli=optional(s,"migrateMilli",p,0,1000,0);
        const auto& diet=list(s,"diet",p,64);
        for(std::size_t k=0;k<diet.size();++k){
            const auto q=at(p+".diet",k);keys(diet[k],q,{"prey","attackMilli","halfSaturation","conversionMilli"});
            def.diet.push_back({required(diet[k],"prey",q,1,INT64_MAX),optional(diet[k],"attackMilli",q,0,100000,0),optional(diet[k],"halfSaturation",q,1,kEcologyPopulationLimit,1),optional(diet[k],"conversionMilli",q,0,100000,0)});
        }
        std::sort(def.diet.begin(),def.diet.end(),[](const auto& a,const auto& b){return a.prey<b.prey;});
        insertUnique(d.species,def.id,def,p);
    }
    const auto& regions=list(j,"regions",root,kEcologyMaxDefs);
    for(std::size_t i=0;i<regions.size();++i){
        const auto p=at(root+".regions",i);const auto& r=regions[i];keys(r,p,{"id","capacity","neighbors"});
        EcologyRegionDef def;def.id=required(r,"id",p,1,INT64_MAX);def.capacity=pairs(r,"capacity",p,kEcologyPopulationLimit,0);def.neighbors=ids(r,"neighbors",p);
        insertUnique(d.regions,def.id,def,p);
    }
    if(auto e=ecologyDefsError(d);!e.empty())throw JsonError(e);
    return d;
}
inline EcologyAnimalDefs readEcologyAnimals(const Json& j){
    using namespace detail_ecology_data;const std::string root="animals";
    keys(j,root,{"ticksPerHour","bounds","species"});EcologyAnimalDefs d;
    d.ticksPerHour=optional(j,"ticksPerHour",root,1,1000000,60);
    if(j.has("bounds")){
        const auto& b=j["bounds"];if(!b.isArray()||b.size()!=2)throw JsonError(root+".bounds: expected [[minX, minZ], [maxX, maxZ]]");
        d.boundsMin=point(b[0],root+".bounds[0]");d.boundsMax=point(b[1],root+".bounds[1]");
    }
    const auto& species=list(j,"species",root,kEcologyMaxDefs);
    for(std::size_t i=0;i<species.size();++i){
        const auto p=at(root+".species",i);const auto& s=species[i];
        keys(s,p,{"species","prey","fears","walkMilli","runMilli","fleeMilli","huntRangeMilli","catchMilli","wanderMilli","hungerPerTick","grazeGain","huntGain","grazeBelow","huntBelow",
            "fullAt","energyPerTick","sleepGain","sleepBelow","sleepFrom","sleepTo","wanderTicks","huntGiveUpTicks","fleeCalmTicks","huntCooldownTicks","fleeFrom","huntFrom"});
        EcologyAnimalDef a;a.species=required(s,"species",p,1,INT64_MAX);a.prey=ids(s,"prey",p,0);a.fears=ids(s,"fears",p,0);
        a.walkSpeed=metres(s,"walkMilli",p,a.walkSpeed);a.runSpeed=metres(s,"runMilli",p,a.runSpeed);a.fleeDistance=metres(s,"fleeMilli",p,a.fleeDistance);
        a.huntRange=metres(s,"huntRangeMilli",p,a.huntRange);a.catchDistance=metres(s,"catchMilli",p,a.catchDistance);a.wanderRadius=metres(s,"wanderMilli",p,a.wanderRadius);
        auto meter=[&](const char* k,int64_t f){return optional(s,k,p,0,kEcologyMeterMax,f);};
        a.hungerPerTick=meter("hungerPerTick",a.hungerPerTick);a.grazeGain=meter("grazeGain",a.grazeGain);a.huntGain=meter("huntGain",a.huntGain);a.grazeBelow=meter("grazeBelow",a.grazeBelow);
        a.huntBelow=meter("huntBelow",a.huntBelow);a.fullAt=meter("fullAt",a.fullAt);a.energyPerTick=meter("energyPerTick",a.energyPerTick);a.sleepGain=meter("sleepGain",a.sleepGain);a.sleepBelow=meter("sleepBelow",a.sleepBelow);
        a.sleepFrom=optional(s,"sleepFrom",p,-1,23,-1);a.sleepTo=optional(s,"sleepTo",p,-1,23,-1);
        auto ticks=[&](const char* k,int64_t f){return optional(s,k,p,1,10000000,f);};
        a.wanderTicks=ticks("wanderTicks",a.wanderTicks);a.huntGiveUpTicks=ticks("huntGiveUpTicks",a.huntGiveUpTicks);a.fleeCalmTicks=ticks("fleeCalmTicks",a.fleeCalmTicks);a.huntCooldownTicks=ticks("huntCooldownTicks",a.huntCooldownTicks);
        if(s.has("fleeFrom"))a.fleeFrom=choice(s["fleeFrom"],p+".fleeFrom",kAwareness);
        if(s.has("huntFrom"))a.huntFrom=choice(s["huntFrom"],p+".huntFrom",kAwareness);
        insertUnique(d.species,a.species,a,p);
    }
    if(auto e=ecologyAnimalDefsError(d);!e.empty())throw JsonError(e);
    return d;
}
inline EcologyResourceState readEcologyResources(const Json& j){
    using namespace detail_ecology_data;const std::string root="resources";
    if(!j.isArray()||j.size()>1000000)throw JsonError(root+": expected an array");
    EcologyResourceState s;
    for(std::size_t i=0;i<j.size();++i){
        const auto p=at(root,i);const auto& n=j[i];keys(n,p,{"id","resource","region","amount","capacity","regrowPerPeriod","periodTicks","regrowDelayTicks"});
        EcologyResourceNode node;node.id=required(n,"id",p,1,INT64_MAX);node.resource=required(n,"resource",p,1,INT64_MAX);node.region=optional(n,"region",p,0,INT64_MAX,0);
        node.capacity=required(n,"capacity",p,0,kEcologyResourceLimit);node.amount=optional(n,"amount",p,0,node.capacity,node.capacity);
        node.regrowPerPeriod=optional(n,"regrowPerPeriod",p,0,kEcologyResourceLimit,0);node.periodTicks=optional(n,"periodTicks",p,1,100000000,60);node.regrowDelayTicks=optional(n,"regrowDelayTicks",p,0,100000000,0);
        if(!ecologyAddResource(s,node))throw JsonError(p+".id: duplicate id "+std::to_string(node.id));
    }
    return s;
}
}
