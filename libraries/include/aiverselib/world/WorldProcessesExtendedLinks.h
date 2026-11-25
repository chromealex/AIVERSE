// Authored graph edges and producers for the seven additional ECS mechanics.
// All refs are immutable stable map IDs in JSON, resolved at game setup into
// generation-safe ECS Entity handles. No runtime ECS indices enter map files.
#pragma once
#include "WorldProcessesExtendedAuthoring.h"
#include "ImprintTrails.h"
#include "CircuitDevices.h"
#include "SocietyNpcTriggers.h"
#include "WorldProcessesLinkedAuthoring.h"
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::lib::worldprocess::extendedLinks {
inline bool supports(std::string_view id){
    return id=="MorphSpring"||id=="MorphForce"||
           id=="CrowdSlot"||id=="CrowdPreference"||
           id=="ImprintLink"||id=="ImprintDeposit"||id=="ImprintTracker"||
           id=="CircuitWire"||id=="CircuitGate"||
           id=="CircuitSwitch"||id=="CircuitDevice"||
           id=="GranularSource"||id=="EvolvePressure"||
           id=="SocietyContact"||id=="SocietyMessage"||id=="SocietyNpcTrigger";
}
inline Json defaults(std::string_view id){
    if(id=="MorphSpring")return Json::parse(R"({"a":"","b":"","restDx":0,"restDy":0,"restDz":0,"stiffness":400,"enabled":false})");
    if(id=="MorphForce")return Json::parse(R"({"node":"","x":0,"y":0,"z":0,"enabled":false})");
    if(id=="CrowdSlot")return Json::parse(R"({"x":0,"z":0,"enabled":true})");
    if(id=="CrowdPreference")return Json::parse(R"({"agent":"","slot0":"","slot1":"","slot2":"","slot3":"","enabled":false})");
    if(id=="ImprintLink")return Json::parse(R"({"a":"","b":"","transfer":0,"enabled":false})");
    if(id=="ImprintDeposit")return Json::parse(R"({"cell":"","footprint":0,"scent":0,"residue":0,"enabled":false})");
    if(id=="ImprintTracker")return Json::parse(R"({"source":"","cell":"","minimumDistance":25,"footprint":50,"scent":10,"residue":0,"enabled":false})");
    if(id=="CircuitWire")return Json::parse(R"({"from":"","to":"","bidirectional":true,"enabled":false})");
    if(id=="CircuitGate")return Json::parse(R"({"inputA":"","inputB":"","output":"","op":0,"enabled":false})");
    if(id=="CircuitSwitch")return Json::parse(R"({"node":"","closed":false,"enabled":false})");
    if(id=="CircuitDevice")return Json::parse(R"({"node":"","kind":0,"requireSignal":true,"enabled":false})");
    if(id=="GranularSource")return Json::parse(R"({"field":"","x":0,"y":0,"delta":0,"enabled":false})");
    if(id=="EvolvePressure")return Json::parse(R"({"region":"","wear":0,"recovery":0,"enrichment":0,"consumption":0,"contamination":0,"purification":0,"growth":0,"regression":0,"enabled":false})");
    if(id=="SocietyContact")return Json::parse(R"({"source":"","target":"","trust":1000,"enabled":false})");
    if(id=="SocietyMessage")return Json::parse(R"({"source":"","topicsHex":"0000000000000000","enabled":false})");
    if(id=="SocietyNpcTrigger")return Json::parse(R"({"person":"","topicsHex":"0000000000000001","actionId":1,"mode":0,"once":true,"enabled":false})");
    return Json{};
}
inline std::string_view referenceTarget(std::string_view id,std::string_view key){
    if(id=="MorphSpring"&&(key=="a"||key=="b"))return "MorphNode";
    if(id=="MorphForce"&&key=="node")return "MorphNode";
    if(id=="CrowdPreference"){
        if(key=="agent")return "CrowdAgent";
        if(key=="slot0"||key=="slot1"||key=="slot2"||key=="slot3")return "CrowdSlot";
    }
    if(id=="ImprintLink"&&(key=="a"||key=="b"))return "ImprintCell";
    if(id=="ImprintDeposit"&&key=="cell")return "ImprintCell";
    if(id=="ImprintTracker"&&key=="cell")return "ImprintCell";
    if(id=="ImprintTracker"&&key=="source")return "Entity";
    if(id=="CircuitWire"&&(key=="from"||key=="to"))return "CircuitNode";
    if(id=="CircuitGate"&&(key=="inputA"||key=="inputB"||key=="output"))return "CircuitNode";
    if((id=="CircuitSwitch"||id=="CircuitDevice")&&key=="node")return "CircuitNode";
    if(id=="GranularSource"&&key=="field")return "GranularField";
    if(id=="EvolvePressure"&&key=="region")return "EvolveRegion";
    if(id=="SocietyContact"&&(key=="source"||key=="target"))return "SocietyPerson";
    if(id=="SocietyMessage"&&key=="source")return "SocietyPerson";
    if(id=="SocietyNpcTrigger"&&key=="person")return "SocietyPerson";
    return {};
}
inline bool isReference(std::string_view id,std::string_view key){
    return !referenceTarget(id,key).empty();
}
inline bool validValue(std::string_view id,std::string_view key,const Json& v){
    if(!supports(id))return false;
    if(isReference(id,key))return linked::validId(v);
    if(key=="enabled")return v.type()==Json::Type::Bool;
    if(id=="MorphSpring"){
        if(key=="stiffness")return authoring::integerExact(v,0,1000);
        return (key=="restDx"||key=="restDy"||key=="restDz")&&
            authoring::integerExact(v,-morph::kPositionLimit,morph::kPositionLimit);
    }
    if(id=="MorphForce")return (key=="x"||key=="y"||key=="z")&&
        authoring::integerExact(v,-morph::kForceLimit,morph::kForceLimit);
    if(id=="CrowdSlot")return (key=="x"||key=="z")&&
        authoring::integerExact(v,-crowd::kCoord,crowd::kCoord);
    if(id=="ImprintLink")return key=="transfer"&&
        authoring::integerExact(v,0,imprint::kMaxRate);
    if(id=="ImprintDeposit")return (key=="footprint"||key=="scent"||key=="residue")&&
        authoring::integerExact(v,0,imprint::kMaxRate);
    if(id=="ImprintTracker"){
        if(key=="minimumDistance")return authoring::integerExact(v,0,1000000);
        return (key=="footprint"||key=="scent"||key=="residue") &&
            authoring::integerExact(v,0,imprint::kMaxRate);
    }
    if(id=="CircuitWire")return key=="bidirectional"&&v.type()==Json::Type::Bool;
    if(id=="CircuitGate")return key=="op"&&authoring::integerExact(v,0,3);
    if(id=="CircuitSwitch")return key=="closed"&&v.type()==Json::Type::Bool;
    if(id=="CircuitDevice"){
        if(key=="requireSignal")return v.type()==Json::Type::Bool;
        return key=="kind"&&authoring::integerExact(v,0,5);
    }
    if(id=="GranularSource"){
        if(key=="x"||key=="y")return authoring::integerExact(v,0,granular::kMaxDimension-1);
        return key=="delta"&&authoring::integerExact(v,-granular::kMaxSourceRate,
                                                       granular::kMaxSourceRate);
    }
    if(id=="EvolvePressure"){
        return (key=="wear"||key=="recovery"||key=="enrichment"||
                key=="consumption"||key=="contamination"||key=="purification"||
                key=="growth"||key=="regression")&&
               authoring::integerExact(v,0,evolve::kMaxRate);
    }
    if(id=="SocietyContact")return key=="trust"&&authoring::integerExact(v,0,1000);
    if(id=="SocietyMessage")return key=="topicsHex"&&extended::hexKnowledge(v);
    if(id=="SocietyNpcTrigger"){
        if(key=="once")return v.type()==Json::Type::Bool;
        if(key=="topicsHex"){
            std::uint64_t mask=0;
            return extended::hexKnowledge(v,&mask)&&mask!=0;
        }
        if(key=="actionId")return authoring::integerExact(v,1,UINT32_MAX);
        return key=="mode"&&authoring::integerExact(v,0,2);
    }
    return false;
}
inline bool validate(std::string_view id,const Json& doc,std::string* error=nullptr){
    const auto fail=[&](std::string s){if(error)*error=std::move(s);return false;};
    if(!supports(id)||!doc.isObject())return fail("Unknown world-process link type");
    const auto schema=defaults(id);
    if(doc.size()!=schema.size())return fail("Missing or unexpected fields");
    for(const auto& [key,v]:doc.members())
        if(!schema.has(key)||!validValue(id,key,v))return fail("Invalid field "+key);
    if(doc["enabled"].boolean()){
        std::size_t bound=0;
        for(const auto& [key,v]:doc.members()){
            if(isReference(id,key)&&!v.str().empty())++bound;
            if(isReference(id,key)&&v.str().empty() &&
               !(id=="CrowdPreference"&&key!="agent") &&
               !(id=="CircuitGate"&&key=="inputB"&&doc["op"].integer()==3))
                return fail("Enabled component needs a stable target: "+key);
        }
        if(id=="CrowdPreference"&&bound<2)return fail("Preference needs agent and at least one slot");
        if((id=="MorphSpring"||id=="ImprintLink"||id=="CircuitWire"||
            id=="SocietyContact")&&
           (doc[id=="SocietyContact"?"source":id=="CircuitWire"?"from":"a"].str()==
            doc[id=="SocietyContact"?"target":id=="CircuitWire"?"to":"b"].str()))
            return fail("Self-link not allowed");
    }
    return true;
}
inline bool edit(std::string_view id,std::string_view key,const Json& v,Json& doc,
                 std::string* error=nullptr){
    if(!validValue(id,key,v)){if(error)*error="Invalid linked field";return false;}
    if(!validate(id,doc,error))return false;
    Json staged=doc;staged.set(std::string(key),v);
    if(!validate(id,staged,error))return false;
    doc=std::move(staged);return true;
}
inline bool hasExisting(const World& w,Entity e,std::string_view id){
    if(id=="MorphSpring")return w.has<morph::Spring>(e);
    if(id=="MorphForce")return w.has<morph::Force>(e);
    if(id=="CrowdSlot")return w.has<crowd::Slot>(e);
    if(id=="CrowdPreference")return w.has<crowd::Preference>(e);
    if(id=="ImprintLink")return w.has<imprint::Link>(e);
    if(id=="ImprintDeposit")return w.has<imprint::Deposit>(e);
    if(id=="ImprintTracker")return w.has<imprint::Tracker>(e);
    if(id=="CircuitWire")return w.has<circuit::Wire>(e);
    if(id=="CircuitGate")return w.has<circuit::Gate>(e);
    if(id=="CircuitSwitch")return w.has<circuit::Switch>(e);
    if(id=="CircuitDevice")return w.has<circuit::Device>(e);
    if(id=="GranularSource")return w.has<granular::Source>(e);
    if(id=="EvolvePressure")return w.has<evolve::Pressure>(e);
    if(id=="SocietyContact")return w.has<society::Contact>(e);
    if(id=="SocietyMessage")return w.has<society::Message>(e);
    if(id=="SocietyNpcTrigger")return w.has<society::NpcTrigger>(e);
    return false;
}
inline bool hasCompatible(const World& w,Entity e,std::string_view type){
    if(type=="CrowdSlot")return w.has<crowd::Slot>(e);
    if(type=="Entity")return w.alive(e);
    return extended::hasExisting(w,e,type);
}
inline bool apply(World& w,Entity e,std::string_view id,const Json& doc,
                  const std::map<std::string,Entity>& references,std::string* error=nullptr){
    if(!w.alive(e)||!validate(id,doc,error)||hasExisting(w,e,id))return false;
    std::map<std::string,Entity> links;
    for(const auto& [key,v]:doc.members()){
        const auto type=referenceTarget(id,key);
        if(type.empty())continue;
        Entity target=kNullEntity;
        if(!v.str().empty()){
            const auto it=references.find(v.str());
            if(it==references.end()||!w.alive(it->second)||
               !hasCompatible(w,it->second,type)){
                if(error)*error="Unresolved map ID or incompatible target: "+v.str();
                return false;
            }
            target=it->second;
        }
        links.emplace(key,target);
    }
    const auto n=[&](const char* key){return doc[key].integer();};
    const auto enabled=doc["enabled"].boolean();
    const auto ref=[&](const char* key){auto it=links.find(key);return it==links.end()?kNullEntity:it->second;};
    if(id=="MorphSpring")
        w.add<morph::Spring>(e,morph::Spring{ref("a"),ref("b"),
            n("restDx"),n("restDy"),n("restDz"),n("stiffness"),enabled});
    else if(id=="MorphForce")
        w.add<morph::Force>(e,morph::Force{ref("node"),n("x"),n("y"),n("z"),enabled});
    else if(id=="CrowdSlot")
        w.add<crowd::Slot>(e,crowd::Slot{n("x"),n("z"),enabled});
    else if(id=="CrowdPreference"){
        crowd::Preference p;
        p.agent=ref("agent");
        p.options[0]=ref("slot0");p.options[1]=ref("slot1");
        p.options[2]=ref("slot2");p.options[3]=ref("slot3");
        p.enabled=enabled;w.add<crowd::Preference>(e,p);
    }else if(id=="ImprintLink")
        w.add<imprint::Link>(e,imprint::Link{ref("a"),ref("b"),n("transfer"),enabled});
    else if(id=="ImprintDeposit")
        w.add<imprint::Deposit>(e,imprint::Deposit{ref("cell"),
            n("footprint"),n("scent"),n("residue"),enabled});
    else if(id=="ImprintTracker"){
        imprint::Tracker t;
        t.source=ref("source");t.cell=ref("cell");
        t.minimumDistance=n("minimumDistance");
        t.footprint=n("footprint");t.scent=n("scent");
        t.residue=n("residue");t.enabled=enabled;
        w.add<imprint::Tracker>(e,t);
    }
    else if(id=="CircuitWire")
        w.add<circuit::Wire>(e,circuit::Wire{ref("from"),ref("to"),
            doc["bidirectional"].boolean(),enabled});
    else if(id=="CircuitGate")
        w.add<circuit::Gate>(e,circuit::Gate{ref("inputA"),ref("inputB"),
            ref("output"),circuit::Op(std::uint8_t(n("op"))),enabled});
    else if(id=="CircuitSwitch")
        w.add<circuit::Switch>(e,circuit::Switch{
            ref("node"),doc["closed"].boolean(),enabled});
    else if(id=="CircuitDevice"){
        circuit::Device device;
        device.node=ref("node");device.enabled=enabled;
        device.requireSignal=doc["requireSignal"].boolean();
        device.kind=circuit::DeviceKind(std::uint8_t(n("kind")));
        w.add<circuit::Device>(e,device);
    }
    else if(id=="GranularSource")
        w.add<granular::Source>(e,granular::Source{ref("field"),
            std::uint16_t(n("x")),std::uint16_t(n("y")),n("delta"),enabled});
    else if(id=="EvolvePressure")
        w.add<evolve::Pressure>(e,evolve::Pressure{ref("region"),n("wear"),
            n("recovery"),n("enrichment"),n("consumption"),n("contamination"),
            n("purification"),n("growth"),n("regression"),enabled});
    else if(id=="SocietyContact")
        w.add<society::Contact>(e,society::Contact{ref("source"),ref("target"),
            std::uint16_t(n("trust")),enabled});
    else if(id=="SocietyNpcTrigger"){
        society::NpcTrigger t;
        t.person=ref("person");t.enabled=enabled;
        t.actionId=std::uint32_t(n("actionId"));
        t.mode=society::TriggerMode(std::uint8_t(n("mode")));
        t.once=doc["once"].boolean();
        if(!extended::hexKnowledge(doc["topicsHex"],&t.topics))return false;
        w.add<society::NpcTrigger>(e,t);
    }else if(id=="SocietyMessage"){
        society::Message m;
        m.source=ref("source");m.enabled=enabled;
        if(!extended::hexKnowledge(doc["topicsHex"],&m.topics))return false;
        w.add<society::Message>(e,m);
    }else return false;
    return true;
}
} // namespace aiverse::lib::worldprocess::extendedLinks
