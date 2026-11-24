// Strict native authoring IR for SpectraFX volume domain. Separate from
// particle-only graph compiler until the unified hybrid runtime is installed.
#pragma once
#include "SpectraVolumeGrid.h"
#include "SpectraSdfBinary.h"
#include "engine/data/Json.h"
#include <string>
#include <initializer_list>
#include <optional>

namespace aiverse::spectra::volume {
struct AuthoredLight {
    float3 position{},color{1,1,1};
    float radius=5,intensity=0;
    bool valid()const{return bounded(position,1e6f)&&bounded(color,64)&&color.x>=0&&color.y>=0&&color.z>=0&&
        between(radius,.01f,1000)&&between(intensity,0,100);}
};
struct Asset {
    Config simulation;
    Material material;
    std::vector<AuthoredLight> lights;
    std::vector<Source> sources;
    std::optional<Source> impulse;
    float impulseSeconds=1;
    std::string obstacle;
    float physisWindResponse=0,physisGravityBlend=0;
    std::string attachment="world";
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
    if(!keys(root,{"format","version","simulation","material","lights","sources","obstacle","impulse","impulseSeconds","physisWindResponse","physisGravityBlend","attachment"})||
       root.getStr("format")!="aiverse.spectrafx.volume"||!root["version"].isNumber()||root["version"].toDouble()!=1)
        return fail("Expected SpectraFX volume asset v1");
    Asset next;
    if(root.has("attachment")){
        if(!root["attachment"].isString())return fail("Invalid volume attachment");
        next.attachment=root["attachment"].str();
        if(next.attachment!="world"&&next.attachment!="follow")return fail("Invalid volume attachment");
    }
    if(!number(root,"physisWindResponse",next.physisWindResponse)||
       !number(root,"physisGravityBlend",next.physisGravityBlend)||
       !between(next.physisWindResponse,0,20)||!between(next.physisGravityBlend,0,1))
        return fail("Invalid PHYSIS volume settings");
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
        if(!keys(m,{"smokeColor","fireColor","extinction","emission","emissionThreshold","smokeDetail","smokeDetailVelocity","emberIntensity"})||
           !vector(m,"smokeColor",a.smokeColor)||!vector(m,"fireColor",a.fireColor)||
           !number(m,"extinction",a.extinction)||!number(m,"emission",a.emission)||
           !number(m,"emissionThreshold",a.emissionThreshold)||
           !vector(m,"smokeDetailVelocity",a.smokeDetailVelocity)||!number(m,"smokeDetail",a.smokeDetail)||!number(m,"emberIntensity",a.emberIntensity)||
           !a.valid())return fail("Invalid volume material");
    }
    if(root.has("lights")){
        const auto& lights=root["lights"];
        if(!lights.isArray()||lights.size()>4)return fail("Invalid volume light count");
        for(const auto& entry:lights.items()){
            AuthoredLight light;
            if(!keys(entry,{"position","color","radius","intensity"})||
               !vector(entry,"position",light.position)||!vector(entry,"color",light.color)||
               !number(entry,"radius",light.radius)||!number(entry,"intensity",light.intensity)||
               !light.valid())return fail("Invalid volume light");
            next.lights.push_back(light);
        }
    }
    const auto parseSource=[](const Json& entry,Source& a){
        return keys(entry,{"position","radius","smoke","temperature","fuel","acceleration","radialAcceleration","flickerHz"})&&
            vector(entry,"position",a.position)&&number(entry,"radius",a.radius)&&
            number(entry,"smoke",a.smoke)&&number(entry,"temperature",a.temperature)&&
            number(entry,"flickerHz",a.flickerHz)&&number(entry,"fuel",a.fuel)&&number(entry,"radialAcceleration",a.radialAcceleration)&&vector(entry,"acceleration",a.acceleration)&&a.valid();
    };
    if(root.has("impulse")){
        Source impulse;
        if(!parseSource(root["impulse"],impulse)||!number(root,"impulseSeconds",next.impulseSeconds)||
           !between(next.impulseSeconds,.001f,1.f))return fail("Invalid volume impulse");
        next.impulse=impulse;
    }else if(root.has("impulseSeconds"))return fail("Impulse duration requires impulse source");
    if(root.has("sources")){
        const auto& sources=root["sources"];
        if(!sources.isArray()||sources.size()>kMaxSources)return fail("Invalid volume source count");
        for(const auto& entry:sources.items()){
            Source a;
            if(!parseSource(entry,a))
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
    s.set("smokeYield",scalar(c.smokeYield));s.set("maxVelocity",scalar(c.maxVelocity));
    root.set("simulation",std::move(s));
    m.set("smokeColor",vec(a.material.smokeColor));m.set("fireColor",vec(a.material.fireColor));
    m.set("extinction",scalar(a.material.extinction));m.set("emission",scalar(a.material.emission));
    m.set("emissionThreshold",scalar(a.material.emissionThreshold));
    if(render::length(a.material.smokeDetailVelocity)>0)m.set("smokeDetailVelocity",vec(a.material.smokeDetailVelocity));
    if(a.material.smokeDetail!=0)m.set("smokeDetail",scalar(a.material.smokeDetail));
    if(a.material.emberIntensity!=0)m.set("emberIntensity",scalar(a.material.emberIntensity));
    root.set("material",std::move(m));
    if(!a.lights.empty()){
        Json lights=Json::array();
        for(const auto& light:a.lights){
            Json item=Json::object();item.set("position",vec(light.position));item.set("color",vec(light.color));
            item.set("radius",scalar(light.radius));item.set("intensity",scalar(light.intensity));lights.push(std::move(item));
        }
        root.set("lights",std::move(lights));
    }
    const auto writeSource=[](const Source& x){
        Json j=Json::object();j.set("position",vec(x.position));j.set("radius",scalar(x.radius));
        j.set("smoke",scalar(x.smoke));j.set("temperature",scalar(x.temperature));j.set("fuel",scalar(x.fuel));
        if(x.flickerHz>0)j.set("flickerHz",scalar(x.flickerHz));
        j.set("acceleration",vec(x.acceleration));j.set("radialAcceleration",scalar(x.radialAcceleration));return j;
    };
    for(const auto& x:a.sources)sources.push(writeSource(x));
    if(a.impulse){root.set("impulse",writeSource(*a.impulse));root.set("impulseSeconds",scalar(a.impulseSeconds));}
    root.set("sources",std::move(sources));
    if(a.attachment!="world")root.set("attachment",Json::string(a.attachment));
    if(a.physisWindResponse!=0)root.set("physisWindResponse",scalar(a.physisWindResponse));
    if(a.physisGravityBlend!=0)root.set("physisGravityBlend",scalar(a.physisGravityBlend));
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
    if(a.impulse&&(!between(a.impulseSeconds,.001f,1.f)||!next.inject(*a.impulse,a.impulseSeconds))){error="Invalid volume impulse";return false;}
    out=std::move(next);error.clear();return true;
}
}
