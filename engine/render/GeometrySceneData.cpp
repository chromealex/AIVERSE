#include "GeometryScene.h"
#include <cmath>
#include <set>
namespace aiverse::render {
namespace {
void number(const Json& j,const char* key,double low,double high){if(j.has(key)&&(!j[key].isNumber()||!std::isfinite(j[key].toDouble())||j[key].toDouble()<low||j[key].toDouble()>high))throw std::runtime_error(std::string("geometry scene: invalid ")+key);}
void path(const std::string& s){if(s.empty()||s.front()=='/'||s.find(':')!=std::string::npos||s.find('\\')!=std::string::npos||s.find("..")!=std::string::npos)throw std::runtime_error("geometry scene: model must be a relative content path");}
}
void GeometryScene::validate(const Json& j){
    if(!j.isObject()||j.getInt("version")!=1||!j["instances"].isArray()||j["instances"].size()>4096)throw std::runtime_error("geometry scene: version 1 and up to 4096 instances required");
    for(auto& [k,v]:j.members())if(k!="version"&&k!="instances"&&k!="hlod")throw std::runtime_error("geometry scene: unknown key '"+k+"'");
    const auto& h=j["hlod"];
    if(!h.isNull()){
        if(!h.isObject())throw std::runtime_error("geometry scene: hlod must be object");
        for(auto& [k,v]:h.members())if(k!="enabled"&&k!="errorPixels"&&k!="maxPixels")throw std::runtime_error("geometry scene: unknown hlod key '"+k+"'");
        if(h.has("enabled")&&h["enabled"].type()!=Json::Type::Bool)throw std::runtime_error("geometry scene: hlod.enabled must be boolean");
        number(h,"errorPixels",.1,16);number(h,"maxPixels",16,2048);
    }
    std::set<std::string> ids;
    for(auto& i:j["instances"].items()){
        if(!i.isObject())throw std::runtime_error("geometry scene: instance must be object");
        for(auto& [k,v]:i.members())if(k!="id"&&k!="model"&&k!="position"&&k!="rotation"&&k!="scale"&&k!="geometry")throw std::runtime_error("geometry scene: unknown instance key '"+k+"'");
        if(i.getStr("id").empty()||!ids.insert(i.getStr("id")).second)throw std::runtime_error("geometry scene: unique nonempty instance id required");
        path(i.getStr("model"));parseGeometryPolicy(i["geometry"]);
        for(auto key:{"position","rotation","scale"})if(i.has(key)){
            const auto& v=i[key];if(!v.isArray()||v.size()!=3)throw std::runtime_error(std::string("geometry scene: expected xyz ")+key);
            for(auto& x:v.items())if(!x.isNumber()||!std::isfinite(x.toDouble())||std::fabs(x.toDouble())>1000000||(std::string(key)=="scale"&&(x.toDouble()<.0001||x.toDouble()>10000)))throw std::runtime_error("geometry scene: invalid transform");
        }
    }
}
}
