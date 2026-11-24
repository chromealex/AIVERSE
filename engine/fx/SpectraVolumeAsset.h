// Strict native authoring IR for SpectraFX volume domain. Separate from
// particle-only graph compiler until the unified hybrid runtime is installed.
#pragma once
#include "SpectraVolumeGrid.h"
#include "SpectraSdfBinary.h"
#include "engine/data/Json.h"
#include <string>
#include <initializer_list>

namespace aiverse::spectra::volume {
struct Asset {
    Config simulation;
    Material material;
    std::vector<Source> sources;
    std::string obstacle;
};
namespace asset_detail {
inline bool keys(const Json& value,std::initializer_list<const char*> allowed){
    if(!value.isObject())return false;
    for(const auto& p:value.members()){
        bool found=false;for(auto key:allowed)found|=p.first==key;
        if(!found)return false;
    }
    return true;
}
inline bool number(const Json& j,const char* key,float& out){
    if(!j.has(key))return true;
    if(!j[key].isNumber()||!std::isfinite(j[key].toDouble()))return false;
    out=float(j[key].toDouble());return std::isfinite(out);
}
inline bool integer(const Json& j,const char* key,uint32_t& out){
    if(!j.has(key))return true;
    if(!j[key].isNumber())return false;
    const auto v=j[key].toDouble();
    if(!std::isfinite(v)||v<0||v>UINT32_MAX||std::floor(v)!=v)return false;
    out=uint32_t(v);return true;
}
inline bool vector(const Json& j,const char* key,float3& out){
    if(!j.has(key))return true;
    const auto& a=j[key];
    if(!a.isArray()||a.size()!=3)return false;
    float v[3]{};
    for(size_t i=0;i<3;++i){
        if(!a[i].isNumber()||!std::isfinite(a[i].toDouble()))return false;
        v[i]=float(a[i].toDouble());if(!std::isfinite(v[i]))return false;
    }
    out={v[0],v[1],v[2]};return true;
}
inline Json scalar(float x){return Json::number(double(x),9);}
inline Json vec(float3 v){auto a=Json::array();a.push(scalar(v.x));a.push(scalar(v.y));a.push(scalar(v.z));return a;}
}
inline bool readAsset(const Json& root,Asset& output,std::string& error){
    using namespace asset_detail;
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(!keys(root,{"format","version","simulation","material","sources","obstacle"})||
       root.getStr("format")!="aiverse.spectrafx.volume"||!root["version"].isNumber()||root["version"].toDouble()!=1)
        return fail("Expected SpectraFX volume asset v1");
    Asset next;
    const auto& s=root["simulation"];auto& c=next.simulation;
    if(!keys(s,{"dimension","pressureIterations","origin","cellSize","gravity","wind","buoyancy","smokeWeight",
               "velocityDecay","smokeDecay","temperatureDecay","fuelDecay","ignition","burnRate","heatYield","smokeYield","maxVelocity"})||
       !integer(s,"dimension",c.dimension)||!integer(s,"pressureIterations",c.pressureIterations)||
       !vector(s,"origin",c.origin)||!number(s,"cellSize",c.cellSize)||
       !vector(s,"gravity",c.gravity)||!vector(s,"wind",c.wind)||
       !number(s,"buoyancy",c.buoyancy)||!number(s,"smokeWeight",c.smokeWeight)||
       !number(s,"velocityDecay",c.velocityDecay)||!number(s,"smokeDecay",c.smokeDecay)||
       !number(s,"temperatureDecay",c.temperatureDecay)||!number(s,"fuelDecay",c.fuelDecay)||
       !number(s,"ignition",c.ignition)||!number(s,"burnRate",c.burnRate)||!number(s,"heatYield",c.heatYield)||
       !number(s,"smokeYield",c.smokeYield)||!number(s,"maxVelocity",c.maxVelocity)||!c.valid())
        return fail("Invalid volume simulation settings");
    if(root.has("material")){
        const auto& m=root["material"];auto& a=next.material;
        if(!keys(m,{"smokeColor","fireColor","extinction","emission","emissionThreshold"})||
           !vector(m,"smokeColor",a.smokeColor)||!vector(m,"fireColor",a.fireColor)||
           !number(m,"extinction",a.extinction)||!number(m,"emission",a.emission)||
           !number(m,"emissionThreshold",a.emissionThreshold)||!a.valid())return fail("Invalid volume material");
    }
    if(root.has("sources")){
        const auto& sources=root["sources"];
        if(!sources.isArray()||sources.size()>kMaxSources)return fail("Invalid volume source count");
        for(const auto& entry:sources.items()){
            Source a;
            if(!keys(entry,{"position","radius","smoke","temperature","fuel","acceleration"})||
               !vector(entry,"position",a.position)||!number(entry,"radius",a.radius)||
               !number(entry,"smoke",a.smoke)||!number(entry,"temperature",a.temperature)||
               !number(entry,"fuel",a.fuel)||!vector(entry,"acceleration",a.acceleration)||!a.valid())
                return fail("Invalid volume source");
            next.sources.push_back(a);
        }
    }
    if(root.has("obstacle")){
        if(!root["obstacle"].isString()||!fx::validSdfResourcePath(root["obstacle"].str()))
            return fail("Invalid volume obstacle path");
        next.obstacle=root["obstacle"].str();
    }
    output=std::move(next);error.clear();return true;
}
inline Json writeAsset(const Asset& a){
    using namespace asset_detail;
    Json root=Json::object(),s=Json::object(),m=Json::object(),sources=Json::array();
    root.set("format",Json::string("aiverse.spectrafx.volume"));root.set("version",Json::number(int64_t(1)));
    const auto& c=a.simulation;
    s.set("dimension",Json::number(int64_t(c.dimension)));s.set("pressureIterations",Json::number(int64_t(c.pressureIterations)));
    s.set("origin",vec(c.origin));s.set("cellSize",scalar(c.cellSize));s.set("gravity",vec(c.gravity));s.set("wind",vec(c.wind));
    s.set("buoyancy",scalar(c.buoyancy));s.set("smokeWeight",scalar(c.smokeWeight));s.set("velocityDecay",scalar(c.velocityDecay));
    s.set("smokeDecay",scalar(c.smokeDecay));s.set("temperatureDecay",scalar(c.temperatureDecay));s.set("fuelDecay",scalar(c.fuelDecay));
    s.set("ignition",scalar(c.ignition));s.set("burnRate",scalar(c.burnRate));s.set("heatYield",scalar(c.heatYield));
    s.set("smokeYield",scalar(c.smokeYield));s.set("maxVelocity",scalar(c.maxVelocity));root.set("simulation",std::move(s));
    m.set("smokeColor",vec(a.material.smokeColor));m.set("fireColor",vec(a.material.fireColor));
    m.set("extinction",scalar(a.material.extinction));m.set("emission",scalar(a.material.emission));
    m.set("emissionThreshold",scalar(a.material.emissionThreshold));root.set("material",std::move(m));
    for(const auto& x:a.sources){
        Json j=Json::object();j.set("position",vec(x.position));j.set("radius",scalar(x.radius));
        j.set("smoke",scalar(x.smoke));j.set("temperature",scalar(x.temperature));j.set("fuel",scalar(x.fuel));
        j.set("acceleration",vec(x.acceleration));sources.push(std::move(j));
    }
    root.set("sources",std::move(sources));
    if(!a.obstacle.empty())root.set("obstacle",Json::string(a.obstacle));
    return root;
}
// Atomic creation; explicit resource owner; no filesystem or RHI authority.
inline bool initAsset(const Asset& a,GasGrid& out,std::string& error,const fx::SdfResourceReader& reader={}){
    if(!a.simulation.valid()||!a.material.valid()||a.sources.size()>kMaxSources){error="Invalid volume asset";return false;}
    GasGrid next;
    if(!next.init(a.simulation)||!next.setSources(a.sources)){error="Invalid volume simulation or sources";return false;}
    if(!a.obstacle.empty()){
        fx::StaticSdfVolume sdf;
        if(!fx::validSdfResourcePath(a.obstacle)||!reader){error="Volume obstacle requires safe resource reader";return false;}
        if(!fx::loadSdfBinary(a.obstacle,reader,sdf,error)||!next.setObstacle(sdf)){
            if(error.empty())error="Invalid volume obstacle";return false;
        }
    }
    out=std::move(next);error.clear();return true;
}
}
