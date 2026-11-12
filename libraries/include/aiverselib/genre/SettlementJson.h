#pragma once
// Strict JSON readers shared by settlement-production (ColonyData.h, bots/ColonyPlannerData.h) and survival-ecology
// (EcologyData.h): integers only (no fractions), unknown keys rejected, every error path-qualified.
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace aiverse::lib::genre::detail_settlement_json {
inline std::string at(const std::string& path,std::size_t i){return path+"["+std::to_string(i)+"]";}
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline int64_t required(const Json& j,const char* key,const std::string& path,int64_t low,int64_t high){
    if(!j.has(key))throw JsonError(path+"."+key+": required");
    return integer(j[key],path+"."+key,low,high);
}
inline int64_t optional(const Json& j,const char* key,const std::string& path,int64_t low,int64_t high,int64_t fallback){return j.has(key)?integer(j[key],path+"."+key,low,high):fallback;}
inline bool flag(const Json& j,const char* key,const std::string& path,bool fallback){
    if(!j.has(key))return fallback;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");
    return j[key].boolean();
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;if(std::find(names.begin(),names.end(),std::string_view(key))==names.end())throw JsonError(path+": unknown key '"+key+"'");}
}
inline const Json& list(const Json& j,const char* key,const std::string& path,std::size_t limit){
    static const Json empty=Json::array();
    if(!j.has(key))return empty;
    if(!j[key].isArray()||j[key].size()>limit)throw JsonError(path+"."+key+": expected an array of at most "+std::to_string(limit)+" entries");
    return j[key];
}
// [[id, amount], ...] with unique positive ids.
inline std::map<int64_t,int64_t> pairs(const Json& j,const char* key,const std::string& path,int64_t maxAmount,int64_t minAmount=1){
    std::map<int64_t,int64_t> out;const auto& a=list(j,key,path,256);const auto p=path+"."+key;
    for(std::size_t i=0;i<a.size();++i){
        if(!a[i].isArray()||a[i].size()!=2)throw JsonError(at(p,i)+": expected [id, amount]");
        auto id=integer(a[i][0],at(p,i)+"[0]",1,INT64_MAX),n=integer(a[i][1],at(p,i)+"[1]",minAmount,maxAmount);
        if(!out.emplace(id,n).second)throw JsonError(at(p,i)+"[0]: duplicate id "+std::to_string(id));
    }
    return out;
}
inline std::vector<int64_t> ids(const Json& j,const char* key,const std::string& path,int64_t low=1){
    std::vector<int64_t> out;const auto& a=list(j,key,path,4096);const auto p=path+"."+key;
    for(std::size_t i=0;i<a.size();++i)out.push_back(integer(a[i],at(p,i),low,INT64_MAX));
    std::sort(out.begin(),out.end());if(std::adjacent_find(out.begin(),out.end())!=out.end())throw JsonError(p+": duplicate id");
    return out;
}
template<class E,std::size_t N> E choice(const Json& j,const std::string& path,const std::array<std::pair<std::string_view,E>,N>& names){
    if(!j.isString())throw JsonError(path+": expected string");
    for(auto [n,e]:names)if(n==j.str())return e;
    std::string all;for(auto [n,e]:names){(void)e;all+=(all.empty()?"":", ")+std::string(n);}
    throw JsonError(path+": unknown value '"+j.str()+"' (expected "+all+")");
}
// Inserts by id, rejecting duplicates with the entry path.
template<class M,class T> void insertUnique(M& m,int64_t id,T value,const std::string& path){if(!m.emplace(id,std::move(value)).second)throw JsonError(path+".id: duplicate id "+std::to_string(id));}
}
