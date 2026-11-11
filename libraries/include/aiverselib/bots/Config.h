#pragma once
#include "engine/data/Json.h"
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::bots {
// Project-local bot configuration, not additional engine rules keys.
inline void keys(const Json& j,std::initializer_list<std::string_view> allowed) {
    if(!j.isObject())throw JsonError("bots: expected object");
    for(const auto& [key,value]:j.members()) {
        (void)value;bool found=false;for(auto name:allowed)if(key==name)found=true;
        if(!found)throw JsonError("bots: unknown setting '"+key+"'");
    }
}
inline int64_t integer(const Json& j,const char* key,int64_t fallback,int64_t lo,int64_t hi) {
    if(!j.has(key)){if(fallback<lo||fallback>hi)throw JsonError(std::string("bots.")+key+": incompatible default; set explicitly");return fallback;}
    if(!j[key].isNumber()||j[key].fx().raw%Fixed::kOne)throw JsonError(std::string("bots.")+key+": expected integer");
    auto v=j[key].fx().floorInt();if(v<lo||v>hi)throw JsonError(std::string("bots.")+key+": outside range");return v;
}
inline Fixed number(const Json& j,const char* key,Fixed fallback,Fixed lo,Fixed hi) {
    if(!j.has(key)){if(fallback<lo||fallback>hi)throw JsonError(std::string("bots.")+key+": incompatible default; set explicitly");return fallback;}
    if(!j[key].isNumber())throw JsonError(std::string("bots.")+key+": expected number");
    auto v=j[key].fx();if(v<lo||v>hi)throw JsonError(std::string("bots.")+key+": outside range");return v;
}
inline std::string preset(const Json& j) {
    if(j.has("preset")&&!j["preset"].isString())throw JsonError("bots.preset: expected string");
    return j.getStr("preset","normal");
}
}
