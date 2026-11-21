// Read-only presentation compilation from canonical authored water entities.
// Legacy top-level manifests remain supported. Entity water currently translates
// as one local-space object; rotation/non-unit scale are rejected, never ignored.
#pragma once
#include "engine/data/Json.h"
#include "engine/core/FixedMath.h"
namespace aiverse::world {
inline Json nereidEntityManifest(const Json& root) {
    if(root.has("nereidWater")&&!root["nereidWater"].isObject())throw JsonError("nereidWater: expected object");
    auto out=root["nereidWater"].isObject()?root["nereidWater"]:Json::object();
    if(out.has("surfaces")&&!out["surfaces"].isArray())throw JsonError("NEREID surfaces must be array");
    auto surfaces=out["surfaces"].isArray()?out["surfaces"]:Json::array();
    for(const auto& entity:root["entities"].items()) {
        const auto& components=entity["components"];
        if(!components.has("NereidWater"))continue;
        const auto& t=components["Transform"];
        if(!t.isObject()||t.getFx("yaw")!=0_fx||t.getFx("scale",1_fx)!=1_fx)throw JsonError("NEREID entity supports translation only");
        const auto& pos=entity["pos"];
        if(!pos.isArray()||pos.size()!=2||!pos[0].isNumber()||!pos[1].isNumber())throw JsonError("NEREID entity requires pos [x,z]");
        const auto x=pos[0].fx(),z=pos[1].fx(),y=t.getFx("y");
        auto surface=components["NereidWater"];
        if(!surface.isObject())throw JsonError("NEREID entity requires surface component");
        const auto number=[](Fixed v){return Json::number(v.toDouble(),6);};
        auto pair=[&](const Json& original) {if(!original.isArray()||original.size()<2)throw JsonError("NEREID local pair");auto v=original;v.at(0)=number(v[0].fx()+x);v.at(1)=number(v[1].fx()+z);return v;};
        if(surface.has("points")){auto points=surface["points"];for(auto& p:points.itemsMut())p=pair(p);surface.set("points",points);}
        if(surface.has("center")){auto c=surface["center"];if(!c.isArray()||c.size()!=3)throw JsonError("NEREID center");c.at(0)=number(c[0].fx()+x);c.at(1)=number(c[1].fx()+y);c.at(2)=number(c[2].fx()+z);surface.set("center",c);}
        else {auto c=Json::array();c.push(number(x));c.push(number(y));c.push(number(z));surface.set("center",c);}
        for(const auto* key:{"surfaceY","bottomY"}){if(!surface[key].isNumber())throw JsonError("NEREID entity water height");surface.set(key,number(surface[key].fx()+y));}
        if(surface.has("grade")){auto grade=surface["grade"];auto origin=grade["origin"];if(!origin.isArray()){origin=Json::array();origin.push(number(0_fx));origin.push(number(0_fx));}grade.set("origin",pair(origin));surface.set("grade",grade);}
        // Vortex positions are local X/Z too.
        if(surface["vortices"].isArray()){auto vortices=surface["vortices"];for(auto& v:vortices.itemsMut())v=pair(v);surface.set("vortices",vortices);}
        surfaces.push(surface);
    }
    if(surfaces.size())out.set("surfaces",surfaces);
    return out;
}
} // namespace aiverse::world
