#pragma once
#include "Service.h"
#include <charconv>
#include <stdexcept>
namespace aiverse::meta::detail {
inline void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error("meta service: "+why); }
inline void keys(const Json& j,std::initializer_list<const char*> allowed) {
    require(j.isObject(),"expected object"); std::set<std::string> seen;
    for(auto& [k,v]:j.members()) { (void)v; bool found=false; for(auto a:allowed) if(k==a) found=true;
        require(found&&seen.insert(k).second,"unknown or duplicate key '"+k+"'"); }
}
inline int64_t integer(const Json& j,int64_t lo=0,int64_t hi=kMaxAmount) {
    require(j.isNumber(),"expected integer"); int64_t n=0; auto& s=j.numberLiteral();
    auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    require(r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&n>=lo&&n<=hi,"integer range"); return n;
}
inline std::string id(const Json& j) {
    require(j.isString()&&!j.str().empty()&&j.str().size()<=128,"expected id (1..128 bytes)"); return j.str();
}
inline Bundle bundle(const Json& j) {
    require(j.isObject(),"expected bundle"); Bundle b;
    for(auto& [k,v]:j.members()) { require(!k.empty()&&!b.count(k),"asset id"); b[k]=integer(v); } return b;
}
inline void checkBundle(const Json& j,const Config& c) {
    for(auto& [k,v]:bundle(j)) require(c.assets.count(k)&&v<=c.assets.at(k).cap,"unknown asset or cap: "+k);
}
inline Json number(int64_t n) { return Json::number(n); }
inline Json str(const std::string& s) { return Json::string(s); }
inline Json& object(Json& j,const std::string& key) { auto& v=j.member(key); require(v.isObject(),"state object: "+key); return v; }
inline Json canonical(const Json& j) {
    if(j.isObject()) { std::map<std::string,Json> m; for(auto& [k,v]:j.members()) {require(!m.count(k),"duplicate key"); m[k]=canonical(v);} auto r=Json::object(); for(auto& [k,v]:m) r.set(k,v); return r; }
    if(j.isArray()) { auto r=Json::array(); for(auto& v:j.items()) r.push(canonical(v)); return r; } return j;
}
inline ServiceReply fail(Code c,const std::string& s) { return {{c,s},Json::object()}; }
} // namespace aiverse::meta::detail
