// Authoring property subset. Native compilers remain authoritative.
#pragma once
#include "engine/data/Json.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace aiverse::editor::graph_properties {
inline bool number(const Json& v){return v.isNumber()&&std::isfinite(v.toDouble());}
inline bool vector(const Json& v,size_t n){
    if(!v.isArray()||v.size()!=n)return false;
    for(const auto& x:v.items())if(!number(x))return false;
    return true;
}
inline Json schema(const std::string& family,const Json& n){
    Json fields=Json::object();
    auto add=[&](const char* name,const char* type){Json f=Json::object();f.set("type",Json::string(type));if(n.has(name))f.set("value",n[name]);fields.set(name,f);};
    if(family=="shader"){
        const auto kind=n.getStr("op");
        if(kind=="constant"||kind=="parameter")add("value","scalar-or-rgba");
        if(kind=="parameter")add("name","nonempty-string");
        if(kind=="sampleTexture"||kind=="sampleTextureLod")add("texture","project-path");
        if(kind=="swizzle")add("mask","rgba-swizzle");
        add("position","vec2");
    }else if(family=="motionweave"){
        const auto kind=n.getStr("kind");
        if(kind=="clip"){add("clip","nonempty-string");add("speed","nonnegative-number");add("loop","boolean");}
        if(kind=="blend"||kind=="additive"||kind=="twoBoneIK"){add("weight","unit-number");add("parameter","string");}
        if(kind=="blend")add("sync","boolean");
        if(kind=="subgraph")add("graph","nonempty-string");
        add("position","vec2");
    }else if(family=="modeling"){
        add("translation","vec3");add("rotation","vec4");add("scale","positive-vec3");
        add("material","string");add("construction","boolean");
    }
    return fields;
}
inline bool projectPath(const std::string& s){
    return !s.empty()&&s.size()<=512&&s.front()!='/'&&s.find("..")==std::string::npos&&s.find('\\')==std::string::npos&&s.find(':')==std::string::npos&&s.find('\0')==std::string::npos;
}
inline bool valid(const std::string& type,const Json& v){
    if(type=="boolean")return v.type()==Json::Type::Bool;
    if(type=="string"||type=="nonempty-string")return v.isString()&&v.str().size()<=256&&(type=="string"||!v.str().empty());
    if(type=="project-path")return v.isString()&&projectPath(v.str());
    if(type=="rgba-swizzle")return v.isString()&&v.str().size()==4&&v.str().find_first_not_of("rgba")==std::string::npos;
    if(type=="scalar-or-rgba")return number(v)||vector(v,4);
    if(type=="vec2")return vector(v,2);
    if(type=="vec3")return vector(v,3);
    if(type=="vec4")return vector(v,4);
    if(type=="positive-vec3"){
        if(!vector(v,3))return false;
        for(const auto& x:v.items())if(x.toDouble()<=1e-6)return false;
        return true;
    }
    if(type=="nonnegative-number")return number(v)&&v.toDouble()>=0;
    if(type=="unit-number")return number(v)&&v.toDouble()>=0&&v.toDouble()<=1;
    return false;
}
inline void set(const std::string& family,Json& node,const Json& edit){
    const auto key=edit.getStr("property");const auto fields=schema(family,node);
    if(!fields.has(key)||!edit.has("value")||!valid(fields[key].getStr("type"),edit["value"]))
        throw std::invalid_argument("unsupported property or invalid typed value");
    node.set(key,edit["value"]);
}
} // namespace aiverse::editor::graph_properties
