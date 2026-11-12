#pragma once
// JSON hitbox definitions: {"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":2,"to":4,"shape":"capsule",
//   "a":[0,1000,500],"b":[0,1000,1500],"radiusMilli":300}]}]}. Coordinates are attacker-local milli-metres
// (+Z forward, +X right, +Y up); "half" (box half extents) and "yawMilli" (turns) are optional; frames are attack ages.
#include "CombatActions.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail {
inline int64_t hitboxInteger(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline void hitboxKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){
        (void)value;bool found=false;for(auto name:names)if(name==key)found=true;
        if(!found)throw JsonError(path+": unknown key '"+key+"'");
    }
}
inline Vec3 hitboxVector(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isArray()||j.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-units");
    auto c=[&](std::size_t i){return Fixed::fromRatio(hitboxInteger(j[i],path+"["+std::to_string(i)+"]",low,high),1000);};
    return {c(0),c(1),c(2)};
}
}
inline HitboxSet readHitboxes(const Json& j,const AttackSet& defs){
    using namespace detail;
    hitboxKeys(j,"hitboxes",{"attacks"});
    if(!valid(defs))throw JsonError("hitboxes: attack definitions are invalid");
    if(!j["attacks"].isArray())throw JsonError("hitboxes.attacks: expected array");
    HitboxSet out;
    for(const auto& entry:j["attacks"].items()){
        hitboxKeys(entry,"hitboxes.attacks[]",{"attack","hitboxes"});
        auto attack=hitboxInteger(entry["attack"],"hitboxes.attacks[].attack",1,INT64_MAX);
        auto base="hitboxes.attack "+std::to_string(attack);
        auto def=defs.find(attack);if(def==defs.end())throw JsonError(base+": unknown attack");
        if(out.contains(attack))throw JsonError(base+": duplicate attack");
        if(!entry["hitboxes"].isArray()||entry["hitboxes"].size()==0||entry["hitboxes"].size()>kCombatMaxShapes)throw JsonError(base+".hitboxes: expected 1..64 hitboxes");
        auto& list=out[attack];
        for(const auto& raw:entry["hitboxes"].items()){
            hitboxKeys(raw,base+".hitboxes[]",{"id","from","to","shape","a","b","half","radiusMilli","yawMilli"});
            HitboxDef h;h.id=hitboxInteger(raw["id"],base+".hitboxes[].id",1,INT64_MAX);
            auto path=base+".hitbox "+std::to_string(h.id);
            if(!list.empty()&&h.id<=list.back().id)throw JsonError(path+": ids must be unique and ascending");
            const auto& d=def->second;
            h.from=uint32_t(hitboxInteger(raw["from"],path+".from",0,3600));h.to=uint32_t(hitboxInteger(raw["to"],path+".to",0,3600));
            if(h.from>=h.to||h.from<d.windup||h.to>d.windup+d.active)
                throw JsonError(path+": frames must lie inside the active window "+std::to_string(d.windup)+".."+std::to_string(d.windup+d.active));
            auto kind=raw["shape"].str();
            if(kind=="sphere")h.shape.kind=ShapeKind::Sphere;else if(kind=="capsule")h.shape.kind=ShapeKind::Capsule;else if(kind=="box")h.shape.kind=ShapeKind::Box;
            else throw JsonError(path+".shape: expected sphere, capsule or box");
            h.shape.a=hitboxVector(raw["a"],path+".a",-100000000,100000000);
            if(h.shape.kind==ShapeKind::Capsule)h.shape.b=hitboxVector(raw["b"],path+".b",-100000000,100000000);
            else if(raw.has("b"))throw JsonError(path+".b: only capsules have a second point");
            else h.shape.b=h.shape.a;
            if(h.shape.kind==ShapeKind::Box){
                h.shape.half=hitboxVector(raw["half"],path+".half",0,1000000);
                if(raw.has("radiusMilli"))throw JsonError(path+".radiusMilli: boxes have no radius");
            }else{
                if(raw.has("half"))throw JsonError(path+".half: only boxes have half extents");
                h.shape.radius=Fixed::fromRatio(hitboxInteger(raw["radiusMilli"],path+".radiusMilli",0,1000000),1000);
            }
            if(raw.has("yawMilli"))h.shape.yaw=Fixed::fromRatio(hitboxInteger(raw["yawMilli"],path+".yawMilli",-1000,1000),1000);
            list.push_back(h);
        }
    }
    if(!valid(defs,out))throw JsonError("hitboxes: invalid hitbox set");
    return out;
}
}
