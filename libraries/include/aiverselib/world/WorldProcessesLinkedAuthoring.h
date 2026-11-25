// Bounded authored object links for ECHO / FRACTURE / AETHER.
// Document IDs are stable map entity IDs, NOT ECS pool indices or asset paths.
#pragma once
#include "WorldProcessesAuthoring.h"
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::lib::worldprocess::linked {

inline bool supports(std::string_view id){
    return id=="EchoPortal"||id=="EchoEmitter"||id=="EchoReceiver"||
           id=="FractureBond"||id=="AetherDuct"||id=="AetherFlux";
}
inline std::string_view targetType(std::string_view id){
    if(id=="EchoPortal"||id=="EchoEmitter"||id=="EchoReceiver")return "EchoZone";
    if(id=="FractureBond")return "FractureMember";
    if(id=="AetherDuct"||id=="AetherFlux")return "AetherGasCell";
    return {};
}
inline bool isReference(std::string_view id,std::string_view key){
    if(id=="EchoPortal"||id=="AetherDuct")return key=="from"||key=="to";
    if(id=="FractureBond")return key=="first"||key=="second";
    if(id=="EchoEmitter"||id=="EchoReceiver")return key=="zone";
    if(id=="AetherFlux")return key=="cell";
    return false;
}
inline bool validId(const Json& value){
    if(!value.isString()||value.str().size()>128)return false;
    for(unsigned char c:value.str())if(c<0x20 || c==0x7f)return false;
    return true; // empty is allowed only while disabled
}
inline Json defaults(std::string_view id){
    Json v=Json::object();
    if(id=="EchoPortal"||id=="AetherDuct"){
        v.set("from",Json::string(""));
        v.set("to",Json::string(""));
        if(id=="EchoPortal"){
            v.set("loss",Json::number(int64_t(0)));
            v.set("bidirectional",Json::fromBool(true));
        }else {
            v.set("maximumPerTick",Json::number(int64_t(0)));
            v.set("oneWay",Json::fromBool(false));
        }
        v.set("enabled",Json::fromBool(false));
    }else if(id=="FractureBond"){
        v.set("first",Json::string(""));
        v.set("second",Json::string(""));
        v.set("strength",Json::number(int64_t(1)));
        v.set("damage",Json::number(int64_t(0)));
        v.set("enabled",Json::fromBool(false));
    }else if(id=="EchoEmitter"||id=="EchoReceiver"||id=="AetherFlux"){
        v.set(id=="AetherFlux"?"cell":"zone",Json::string(""));
        if(id=="EchoEmitter")v.set("intensity",Json::number(int64_t(1)));
        else if(id=="EchoReceiver")v.set("threshold",Json::number(int64_t(1)));
        else {
            v.set("smoke",Json::number(int64_t(0)));
            v.set("toxin",Json::number(int64_t(0)));
            v.set("oxygen",Json::number(int64_t(0)));
        }
        v.set("enabled",Json::fromBool(false));
    }
    return v;
}
inline bool validValue(std::string_view id,std::string_view key,const Json& value){
    if(!supports(id))return false;
    if(isReference(id,key))return validId(value);
    if(key=="enabled")return value.type()==Json::Type::Bool;
    if(key=="bidirectional"&&id=="EchoPortal")return value.type()==Json::Type::Bool;
    if(key=="oneWay"&&id=="AetherDuct")return value.type()==Json::Type::Bool;
    if(key=="loss"&&id=="EchoPortal")
        return authoring::integerExact(value,0,echo::kMaxIntensity);
    if(key=="intensity"&&id=="EchoEmitter")
        return authoring::integerExact(value,1,echo::kMaxIntensity);
    if(key=="threshold"&&id=="EchoReceiver")
        return authoring::integerExact(value,0,echo::kMaxIntensity);
    if(key=="strength"&&id=="FractureBond")
        return authoring::integerExact(value,1,fracture::kMaxBondStrength);
    if(key=="damage"&&id=="FractureBond")
        return authoring::integerExact(value,0,fracture::kMaxBondStrength);
    if(key=="maximumPerTick"&&id=="AetherDuct")
        return authoring::integerExact(value,0,aether::kMaxDuctRate);
    if(id=="AetherFlux"&&(key=="smoke"||key=="toxin"||key=="oxygen"))
        return authoring::integerExact(value,-aether::kMaxDuctRate,
                                            aether::kMaxDuctRate);
    return false;
}
inline bool validate(std::string_view id,const Json& values,std::string* error=nullptr){
    const auto fail=[&](std::string reason){
        if(error)*error=std::move(reason);
        return false;
    };
    if(!supports(id)||!values.isObject())return fail("Unknown linked component");
    const auto schema=defaults(id);
    if(values.size()!=schema.size())return fail("Missing or extra linked fields");
    for(const auto& [key,value]:values.members())
        if(!schema.has(key)||!validValue(id,key,value))
            return fail("Invalid linked field: "+key);
    if(id=="FractureBond" &&
       values["damage"].integer()>values["strength"].integer())
        return fail("Bond damage exceeds strength");
    if(values["enabled"].boolean()){
        if(id=="EchoPortal"||id=="AetherDuct"){
            if(values["from"].str().empty()||values["to"].str().empty()||
               values["from"].str()==values["to"].str())
                return fail("Enabled link needs two distinct endpoints");
        }else if(id=="FractureBond"){
            if(values["first"].str().empty()||values["second"].str().empty()||
               values["first"].str()==values["second"].str())
                return fail("Enabled bond needs two distinct members");
        }else if(values[id=="AetherFlux"?"cell":"zone"].str().empty())
            return fail("Enabled source/listener needs a target");
    }
    return true;
}
inline bool edit(std::string_view id,std::string_view key,const Json& value,
                 Json& document,std::string* error=nullptr){
    if(!validValue(id,key,value)) {
        if(error)*error="Invalid linked field";
        return false;
    }
    Json staged=document;
    if(!validate(id,staged,error))return false;
    staged.set(std::string(key),value);
    if(!validate(id,staged,error))return false;
    document=std::move(staged);
    return true;
}
inline bool resolveOne(const World& world,const std::map<std::string,Entity>& ids,
                       const std::string& name,std::string_view type,Entity& result){
    result=kNullEntity;
    if(name.empty())return true;
    const auto it=ids.find(name);
    if(it==ids.end()||!world.alive(it->second))return false;
    const Entity e=it->second;
    if(type=="EchoZone"&&!world.has<echo::AcousticZone>(e))return false;
    if(type=="FractureMember"&&!world.has<fracture::Member>(e))return false;
    if(type=="AetherGasCell"&&!world.has<aether::GasCell>(e))return false;
    result=e;
    return true;
}
inline bool apply(World& world,Entity entity,std::string_view id,
                  const Json& doc,const std::map<std::string,Entity>& ids,
                  std::string* error=nullptr){
    if(!world.alive(entity)||!validate(id,doc,error))return false;
    Entity a=kNullEntity,b=kNullEntity;
    const auto type=targetType(id);
    const std::string first=id=="FractureBond"?"first":
                            id=="AetherFlux"?"cell":
                            id=="EchoEmitter"||id=="EchoReceiver"?"zone":"from";
    const std::string second=id=="FractureBond"?"second":"to";
    if(!resolveOne(world,ids,doc[first].str(),type,a)||
       ((id=="EchoPortal"||id=="FractureBond"||id=="AetherDuct") &&
         !resolveOne(world,ids,doc[second].str(),type,b))){
        if(error)*error="Unresolved or incompatible map entity reference";
        return false;
    }
    if(id=="EchoPortal"){
        if(world.has<echo::AcousticPortal>(entity))return false;
        world.add<echo::AcousticPortal>(entity,
            echo::AcousticPortal{a,b,doc["loss"].integer(),
                                 doc["bidirectional"].boolean(),doc["enabled"].boolean()});
    }else if(id=="EchoEmitter"){
        if(world.has<echo::AcousticEmitter>(entity))return false;
        world.add<echo::AcousticEmitter>(entity,
            echo::AcousticEmitter{a,doc["intensity"].integer(),doc["enabled"].boolean()});
    }else if(id=="EchoReceiver"){
        if(world.has<echo::AcousticReceiver>(entity))return false;
        world.add<echo::AcousticReceiver>(entity,
            echo::AcousticReceiver{a,doc["threshold"].integer(),doc["enabled"].boolean()});
    }else if(id=="FractureBond"){
        if(world.has<fracture::Bond>(entity))return false;
        world.add<fracture::Bond>(entity,
            fracture::Bond{a,b,doc["strength"].integer(),doc["damage"].integer(),
                           doc["enabled"].boolean()});
    }else if(id=="AetherDuct"){
        if(world.has<aether::GasDuct>(entity))return false;
        world.add<aether::GasDuct>(entity,
            aether::GasDuct{a,b,doc["maximumPerTick"].integer(),
                            doc["oneWay"].boolean(),doc["enabled"].boolean()});
    }else if(id=="AetherFlux"){
        if(world.has<aether::GasFlux>(entity))return false;
        world.add<aether::GasFlux>(entity,
            aether::GasFlux{a,doc["smoke"].integer(),doc["toxin"].integer(),
                            doc["oxygen"].integer(),doc["enabled"].boolean()});
    }else return false;
    return true;
}
} // namespace aiverse::lib::worldprocess::linked
