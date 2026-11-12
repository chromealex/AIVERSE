#pragma once
// JSON definitions for stealth-senses: {"disguises":[{"id":1,"factions":[2],"zones":[10],"blownBy":[1],
//   "scaleMilli":200,"trespassScaleMilli":1000}],"hidingSpots":[{"id":5,"position":[0,0,4000],"capacity":1}]}.
// Positions are milli-metres; lists are positive IDs (sorted on load, duplicates rejected). Errors are
// JsonError("senses.<path>: ...").
#include "Senses.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail {
inline int64_t sensesInteger(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline void sensesKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){
        (void)value;bool found=false;for(auto name:names)if(name==key)found=true;
        if(!found)throw JsonError(path+": unknown key '"+key+"'");
    }
}
inline std::vector<int64_t> sensesIds(const Json& j,const std::string& path){
    std::vector<int64_t> out;if(j.isNull())return out;
    if(!j.isArray()||j.size()>kSensesMaxList)throw JsonError(path+": expected an array of at most 64 ids");
    for(std::size_t i=0;i<j.size();++i)out.push_back(sensesInteger(j[i],path+"["+std::to_string(i)+"]",1,INT64_MAX));
    std::sort(out.begin(),out.end());if(std::adjacent_find(out.begin(),out.end())!=out.end())throw JsonError(path+": duplicate id");
    return out;
}
}
inline SenseDefinitions readSenseDefinitions(const Json& j){
    using namespace detail;
    sensesKeys(j,"senses",{"disguises","hidingSpots"});SenseDefinitions out;
    for(auto [key,limit]:{std::pair<const char*,std::size_t>{"disguises",kSensesMaxAgents},{"hidingSpots",kSensesMaxAgents}})
        if(j.has(key)&&(!j[key].isArray()||j[key].size()>limit))throw JsonError(std::string("senses.")+key+": expected an array of at most 4096 entries");
    for(const auto& raw:j["disguises"].items()){
        sensesKeys(raw,"senses.disguises[]",{"id","factions","zones","blownBy","scaleMilli","trespassScaleMilli"});
        DisguiseDef d;d.id=sensesInteger(raw["id"],"senses.disguises[].id",1,INT64_MAX);auto path="senses.disguise "+std::to_string(d.id);
        if(out.disguises.contains(d.id))throw JsonError(path+": duplicate id");
        d.factions=sensesIds(raw["factions"],path+".factions");if(d.factions.empty())throw JsonError(path+".factions: list at least one fooled team");
        d.zones=sensesIds(raw["zones"],path+".zones");d.blownBy=sensesIds(raw["blownBy"],path+".blownBy");
        if(raw.has("scaleMilli"))d.scaleMilli=sensesInteger(raw["scaleMilli"],path+".scaleMilli",0,kSensesMaxScale);
        if(raw.has("trespassScaleMilli"))d.trespassScaleMilli=sensesInteger(raw["trespassScaleMilli"],path+".trespassScaleMilli",0,kSensesMaxScale);
        out.disguises.emplace(d.id,std::move(d));
    }
    for(const auto& raw:j["hidingSpots"].items()){
        sensesKeys(raw,"senses.hidingSpots[]",{"id","position","capacity"});
        HidingSpot h;h.id=sensesInteger(raw["id"],"senses.hidingSpots[].id",1,INT64_MAX);auto path="senses.hidingSpot "+std::to_string(h.id);
        if(out.spots.contains(h.id))throw JsonError(path+": duplicate id");
        const auto& p=raw["position"];if(!p.isArray()||p.size()!=3)throw JsonError(path+".position: expected [x,y,z] in milli-metres");
        auto c=[&](std::size_t i){return Fixed::fromRatio(sensesInteger(p[i],path+".position["+std::to_string(i)+"]",-100000000,100000000),1000);};
        h.position={c(0),c(1),c(2)};
        if(raw.has("capacity"))h.capacity=uint32_t(sensesInteger(raw["capacity"],path+".capacity",1,int64_t(kSensesMaxList)));
        out.spots.emplace(h.id,h);
    }
    if(!valid(out))throw JsonError("senses: invalid definitions");
    return out;
}
}
