// SpectraFX graph-v2 named live instance scalar parameters.
// Pure metadata: slot bindings are validated once and never interpreted in
// the per-particle hot path. No ECS ownership, RHI, or genre assumptions.
#pragma once
#include "engine/data/Json.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::spectra {

struct InstanceParameterDef {
    uint32_t slot=0;
    float initial=0;
    float minimum=-1.0e7f;
    float maximum=1.0e7f;
};
using InstanceParameterTable=std::map<std::string,InstanceParameterDef>;

inline bool instanceParameterNameOk(std::string_view name) {
    if(name.empty() || name.size()>64)return false;
    for(char c:name)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
inline bool loadInstanceParameterTable(const Json& source,
                                       InstanceParameterTable& output,
                                       std::vector<std::string>& errors) {
    InstanceParameterTable table;
    if(source.isNull()) {output={};return true;}
    if(!source.isObject()||source.size()>8) {
        errors.push_back("SpectraFX instanceParameters must be an object with at most 8 entries");
        return false;
    }
    std::array<bool,8> slots{};
    for(const auto& [name,item]:source.members()) {
        if(!instanceParameterNameOk(name)||!item.isObject()||
           !item["slot"].isNumber()||
           item["slot"].numberLiteral().find_first_of(".eE")!=std::string::npos) {
            errors.push_back("invalid instance parameter declaration '"+name+"'");
            return false;
        }
        for(const auto& [key,v]:item.members()) {
            (void)v;
            if(key!="slot"&&key!="default"&&key!="min"&&key!="max") {
                errors.push_back("unknown instance parameter setting '"+name+"."+key+"'");
                return false;
            }
        }
        const int64_t raw=item.getInt("slot",-1);
        if(raw<0||raw>=8||slots[size_t(raw)]) {
            errors.push_back("invalid or duplicate instance parameter slot '"+name+"'");
            return false;
        }
        slots[size_t(raw)]=true;
        InstanceParameterDef d;
        d.slot=uint32_t(raw);
        const auto read=[&](const char* key,float fallback,float& out)->bool {
            if(!item.has(key)) {out=fallback;return true;}
            if(!item[key].isNumber())return false;
            const double n=item[key].toDouble();
            if(!std::isfinite(n)||std::abs(n)>1.0e7)return false;
            out=float(n);
            return true;
        };
        if(!read("default",0,d.initial)||!read("min",-1.0e7f,d.minimum)||
           !read("max",1.0e7f,d.maximum)||d.minimum>d.maximum||
           d.initial<d.minimum||d.initial>d.maximum) {
            errors.push_back("invalid range/default for instance parameter '"+name+"'");
            return false;
        }
        table.emplace(name,d);
    }
    output=std::move(table);
    return true;
}
inline bool checkInstanceParameterValue(const InstanceParameterDef& def,float value) {
    return std::isfinite(value) && value>=def.minimum && value<=def.maximum;
}
} // namespace aiverse::spectra
