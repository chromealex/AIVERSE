// Genre-neutral map/Studio authoring for the seven new ECS mechanics.
// No editor dependency; authored fields are distinct from live outputs.
#pragma once
#include "WorldProcessesAuthoring.h"
#include "WorldProcessesSuite.h"
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::lib::worldprocess::extended {
inline bool supports(std::string_view id){
    return id=="MorphNode"||id=="CrowdAgent"||id=="ImprintCell"||
           id=="CircuitNode"||id=="GranularField"||id=="EvolveRegion"||
           id=="SocietyPerson";
}
inline Json defaults(std::string_view id){
    if(id=="MorphNode")return Json::parse(R"({"restX":0,"restY":0,"restZ":0,"drag":100,"anchored":false,"enabled":true})");
    if(id=="CrowdAgent")return Json::parse(R"({"x":0,"z":0,"goalX":0,"goalZ":0,"radius":20,"speed":20,"enabled":true})");
    if(id=="ImprintCell")return Json::parse(R"({"footprint":0,"scent":0,"residue":0,"footprintFade":10,"scentFade":10,"residueFade":1,"enabled":true})");
    if(id=="CircuitNode")return Json::parse(R"({"supply":0,"demand":0,"drive":false,"enabled":true})");
    if(id=="GranularField")return Json::parse(R"({"capacity":1000,"initialMass":0,"enabled":true})");
    if(id=="EvolveRegion")return Json::parse(R"({"durability":1000000000,"fertility":500000000,"succession":0,"pollution":0,"enabled":true})");
    if(id=="SocietyPerson")return Json::parse(R"({"knowledgeHex":"0000000000000000","credibility":1000,"acceptance":250000,"enabled":true})");
    return Json{};
}
inline bool hexKnowledge(const Json& value,std::uint64_t* output=nullptr){
    if(!value.isString()||value.str().size()!=16)return false;
    std::uint64_t bits=0;
    const auto& s=value.str();
    const auto [end,err]=std::from_chars(s.data(),s.data()+s.size(),bits,16);
    if(err!=std::errc{}||end!=s.data()+s.size())return false;
    if(output)*output=bits;
    return true;
}
inline bool validValue(std::string_view id,std::string_view key,const Json& v){
    if(!supports(id))return false;
    if(key=="enabled")return v.type()==Json::Type::Bool;
    if(id=="MorphNode"){
        if(key=="anchored")return v.type()==Json::Type::Bool;
        if(key=="drag")return authoring::integerExact(v,0,1000);
        return (key=="restX"||key=="restY"||key=="restZ") &&
            authoring::integerExact(v,-morph::kPositionLimit,morph::kPositionLimit);
    }
    if(id=="CrowdAgent"){
        if(key=="radius")return authoring::integerExact(v,0,crowd::kMaxRadius);
        if(key=="speed")return authoring::integerExact(v,0,crowd::kMaxSpeed);
        return (key=="x"||key=="z"||key=="goalX"||key=="goalZ") &&
            authoring::integerExact(v,-crowd::kCoord,crowd::kCoord);
    }
    if(id=="ImprintCell"){
        if(key=="footprintFade"||key=="scentFade"||key=="residueFade")
            return authoring::integerExact(v,0,imprint::kMaxRate);
        return (key=="footprint"||key=="scent"||key=="residue") &&
            authoring::integerExact(v,0,imprint::kMaxLevel);
    }
    if(id=="CircuitNode"){
        if(key=="drive")return v.type()==Json::Type::Bool;
        return (key=="supply"||key=="demand")&&
            authoring::integerExact(v,0,circuit::kMaxUnits);
    }
    if(id=="GranularField"){
        if(key=="capacity")return authoring::integerExact(v,1,granular::kMaxCellMass);
        return key=="initialMass"&&authoring::integerExact(v,0,granular::kMaxCellMass);
    }
    if(id=="EvolveRegion"){
        return (key=="durability"||key=="fertility"||key=="succession"||
                key=="pollution")&&authoring::integerExact(v,0,evolve::kMaxState);
    }
    if(id=="SocietyPerson"){
        if(key=="knowledgeHex")return hexKnowledge(v);
        if(key=="credibility")return authoring::integerExact(v,0,1000);
        if(key=="acceptance")return authoring::integerExact(v,0,1000000);
    }
    return false;
}
inline bool validate(std::string_view id,const Json& doc,std::string* error=nullptr){
    const auto fail=[&](std::string s){if(error)*error=std::move(s);return false;};
    if(!supports(id)||!doc.isObject())return fail("Unknown World Processes authoring component");
    const Json schema=defaults(id);
    if(doc.size()!=schema.size())return fail("Missing or extra authoring fields");
    for(const auto& [key,v]:doc.members())
        if(!schema.has(key)||!validValue(id,key,v))
            return fail("Invalid authoring field: "+key);
    if(id=="GranularField"&&doc["initialMass"].integer()>doc["capacity"].integer())
        return fail("Granular initialMass exceeds capacity");
    return true;
}
inline bool edit(std::string_view id,std::string_view key,const Json& v,
                 Json& doc,std::string* error=nullptr){
    if(!validValue(id,key,v)){
        if(error)*error="Invalid world-process field";
        return false;
    }
    if(!validate(id,doc,error))return false;
    Json next=doc;next.set(std::string(key),v);
    if(!validate(id,next,error))return false;
    doc=std::move(next);
    return true;
}
inline bool hasExisting(const World& world,Entity e,std::string_view id){
    if(id=="MorphNode")return world.has<morph::Node>(e);
    if(id=="CrowdAgent")return world.has<crowd::Agent>(e);
    if(id=="ImprintCell")return world.has<imprint::Cell>(e);
    if(id=="CircuitNode")return world.has<circuit::Node>(e);
    if(id=="GranularField")return world.has<granular::Field>(e);
    if(id=="EvolveRegion")return world.has<evolve::Region>(e);
    if(id=="SocietyPerson")return world.has<society::Person>(e);
    return false;
}
inline bool apply(World& world,Entity e,std::string_view id,const Json& doc,
                  std::string* error=nullptr){
    if(!world.alive(e)||!validate(id,doc,error)||hasExisting(world,e,id))return false;
    const auto n=[&](const char* key){return doc[key].integer();};
    const auto flag=[&](const char* key){return doc[key].boolean();};
    if(id=="MorphNode"){
        morph::Node v;
        v.x=v.restX=n("restX");v.y=v.restY=n("restY");v.z=v.restZ=n("restZ");
        v.drag=n("drag");v.anchored=flag("anchored");v.enabled=flag("enabled");
        world.add<morph::Node>(e,v);
    }else if(id=="CrowdAgent"){
        crowd::Agent v;
        v.x=n("x");v.z=n("z");v.goalX=n("goalX");v.goalZ=n("goalZ");
        v.radius=n("radius");v.speed=n("speed");v.enabled=flag("enabled");
        world.add<crowd::Agent>(e,v);
    }else if(id=="ImprintCell"){
        imprint::Cell v;
        v.footprint=n("footprint");v.scent=n("scent");v.residue=n("residue");
        v.footprintFade=n("footprintFade");v.scentFade=n("scentFade");
        v.residueFade=n("residueFade");v.enabled=flag("enabled");
        world.add<imprint::Cell>(e,v);
    }else if(id=="CircuitNode"){
        circuit::Node v;
        v.supply=n("supply");v.demand=n("demand");
        v.drive=flag("drive");v.enabled=flag("enabled");
        world.add<circuit::Node>(e,v);
    }else if(id=="GranularField"){
        granular::Field v;
        v.capacity=n("capacity");v.mass[0]=n("initialMass");
        v.enabled=flag("enabled");
        world.add<granular::Field>(e,v);
    }else if(id=="EvolveRegion"){
        evolve::Region v;
        v.durability=n("durability");v.fertility=n("fertility");
        v.succession=n("succession");v.pollution=n("pollution");
        v.enabled=flag("enabled");
        world.add<evolve::Region>(e,v);
    }else if(id=="SocietyPerson"){
        society::Person v;
        if(!hexKnowledge(doc["knowledgeHex"],&v.knowledge))return false;
        v.credibility=std::uint16_t(n("credibility"));
        v.acceptance=std::uint32_t(n("acceptance"));
        v.enabled=flag("enabled");
        world.add<society::Person>(e,v);
    }else return false;
    return true;
}
} // namespace aiverse::lib::worldprocess::extended
