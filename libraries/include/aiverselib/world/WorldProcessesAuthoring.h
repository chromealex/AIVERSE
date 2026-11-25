// AIVERSE World Processes: document authoring contract for self-contained ECS components.
// Project native setup explicitly applies validated map data to real ECS entities;
// this header has no Studio/Editor/genre dependencies and performs no runtime writes
// on behalf of the UI. Persistent map changes remain owned by MapDoc.
#pragma once
#include "EchoEcs.h"
#include "FractureEcs.h"
#include "AetherEcs.h"
#include "engine/data/Json.h"
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::lib::worldprocess::authoring {
inline bool supports(std::string_view id){
    return id=="EchoZone"||id=="FractureMember"||id=="AetherGasCell";
}
inline Json defaults(std::string_view id) {
    Json out=Json::object();
    if(id=="EchoZone")out.set("enabled",Json::fromBool(true));
    else if(id=="FractureMember")out.set("anchored",Json::fromBool(false));
    else if(id=="AetherGasCell") {
        out.set("smoke",Json::number(int64_t(0)));
        out.set("toxin",Json::number(int64_t(0)));
        out.set("oxygen",Json::number(int64_t(0)));
        out.set("enabled",Json::fromBool(true));
    }
    return out;
}
inline bool integerExact(const Json& v,int64_t lo,int64_t hi) {
    if(!v.isNumber())return false;
    const auto& literal=v.numberLiteral();
    if(literal.empty())return false;
    int64_t parsed=0;
    const auto [end,error]=std::from_chars(literal.data(),literal.data()+literal.size(),parsed);
    return error==std::errc{} && end==literal.data()+literal.size() &&
           parsed>=lo&&parsed<=hi;
}
inline bool validValue(std::string_view id,std::string_view key,const Json& v){
    if(id=="EchoZone")return key=="enabled"&&v.type()==Json::Type::Bool;
    if(id=="FractureMember")return key=="anchored"&&v.type()==Json::Type::Bool;
    if(id=="AetherGasCell") {
        if(key=="enabled")return v.type()==Json::Type::Bool;
        return (key=="smoke"||key=="toxin"||key=="oxygen") &&
               integerExact(v,0,aether::kMaxCellMass);
    }
    return false;
}
// Full specification required; unknown/untyped keys fail closed, not silently
// ignored. Runtime-only fields (detached, broken, sampledTick, etc.) cannot be
// authored or forged by a map edit.
inline bool validate(std::string_view id,const Json& values,
                     std::string* error=nullptr) {
    auto fail=[&](const std::string& message){
        if(error)*error=message;
        return false;
    };
    if(!supports(id))return fail("Unknown authorable world-process component");
    if(!values.isObject())return fail("Expected a component object");
    const Json canonical=defaults(id);
    if(values.size()!=canonical.size())
        return fail("Missing or unexpected component fields");
    for(const auto& [key,value]:values.members()){
        if(!canonical.has(key)||!validValue(id,key,value))
            return fail("Invalid component field: "+key);
    }
    return true;
}
inline bool edit(std::string_view id,std::string_view key,const Json& value,
                 Json& document,std::string* error=nullptr) {
    if(!supports(id)||!validValue(id,key,value)) {
        if(error)*error="Invalid authoring component or field";
        return false;
    }
    Json staged=document;
    if(!validate(id,staged,error))return false;
    staged.set(std::string(key),value);
    if(!validate(id,staged,error))return false;
    document=std::move(staged);
    return true;
}
// Call explicitly from project-native entity creation after the spawn's
// stable authored-document-id -> runtime Entity mapping has been resolved.
// Component is constructed only after full validation. Not installed
// automatically: legacy hosts without this adapter do not claim support.
inline bool apply(World& world,Entity entity,std::string_view id,
                  const Json& document,std::string* error=nullptr) {
    if(!world.alive(entity) || !validate(id,document,error))return false;
    if(id=="EchoZone"){
        if(world.has<echo::AcousticZone>(entity))return false;
        world.add<echo::AcousticZone>(
            entity,echo::AcousticZone{document["enabled"].boolean()});
    }else if(id=="FractureMember"){
        if(world.has<fracture::Member>(entity))return false;
        world.add<fracture::Member>(
            entity,fracture::Member{document["anchored"].boolean(),false,false});
    }else if(id=="AetherGasCell"){
        if(world.has<aether::GasCell>(entity))return false;
        world.add<aether::GasCell>(entity,aether::GasCell{
            document["smoke"].integer(),document["toxin"].integer(),
            document["oxygen"].integer(),
            std::numeric_limits<std::uint32_t>::max(),
            document["enabled"].boolean()
        });
    }else return false;
    return true;
}
} // namespace aiverse::lib::worldprocess::authoring
