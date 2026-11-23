// GRANULAR full-grid document authoring and safe, bounded terrain/tool hooks.
// Editor tools write MapDoc JSON; this adapter validates then creates ECS.
// Does not own TerraWeave terrain or Box3D collision bodies.
#pragma once
#include "GranularEcs.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::granular::authoring {
inline bool boundedInteger(const Json& j,std::int64_t lo,std::int64_t hi){
    if(!j.isNumber())return false;
    const std::string& s=j.numberLiteral();
    std::int64_t out=0;
    auto [ptr,error]=std::from_chars(s.data(),s.data()+s.size(),out);
    return error==std::errc{}&&ptr==s.data()+s.size()&&out>=lo&&out<=hi;
}
inline Json create(std::uint16_t width,std::uint16_t height,I capacity){
    if(width==0||height==0||width>kMaxDimension||
       height>kMaxDimension||capacity<1||capacity>kMaxCellMass)return Json{};
    Json doc=Json::object(),mass=Json::array(),solid=Json::array();
    for(std::size_t i=0;i<std::size_t(width)*height;++i){
        mass.push(Json::number(I(0)));
        solid.push(Json::number(I(0)));
    }
    doc.set("width",Json::number(I(width)));
    doc.set("height",Json::number(I(height)));
    doc.set("capacity",Json::number(capacity));
    doc.set("mass",std::move(mass));
    doc.set("solid",std::move(solid));
    doc.set("enabled",Json::fromBool(true));
    return doc;
}
inline bool validate(const Json& d,std::string* error=nullptr){
    auto fail=[&](std::string why){if(error)*error=std::move(why);return false;};
    if(!d.isObject()||d.size()!=6||
       !boundedInteger(d["width"],1,kMaxDimension)||
       !boundedInteger(d["height"],1,kMaxDimension)||
       !boundedInteger(d["capacity"],1,kMaxCellMass)||
       d["enabled"].type()!=Json::Type::Bool||
       !d["mass"].isArray()||!d["solid"].isArray())
        return fail("Invalid GRANULAR grid header");
    const std::size_t count=std::size_t(d["width"].integer())*
                            std::size_t(d["height"].integer());
    if(d["mass"].size()!=count||d["solid"].size()!=count)
        return fail("GRANULAR cell arrays have wrong dimensions");
    for(const auto& [key,value]:d.members())
        if(key!="width"&&key!="height"&&key!="capacity"&&
           key!="mass"&&key!="solid"&&key!="enabled")
            return fail("Unknown GRANULAR grid key");
    const I capacity=d["capacity"].integer();
    for(std::size_t i=0;i<count;++i){
        if(!boundedInteger(d["mass"][i],0,capacity)||
           !boundedInteger(d["solid"][i],0,1))
            return fail("GRANULAR invalid cell mass/obstacle");
        if(d["solid"][i].integer()==1&&d["mass"][i].integer()!=0)
            return fail("Blocked GRANULAR cell must be empty");
    }
    return true;
}
// Explicit resizing preserves the overlapping region. The caller's
// MapDoc transaction owns Undo/Redo and makes deletion of out-of-bounds
// cells visible to the author. No persistent runtime pointer is held.
inline bool resize(Json& document,std::uint16_t width,std::uint16_t height,
                   std::string* error=nullptr){
    if(!validate(document,error))return false;
    Json next=create(width,height,document["capacity"].integer());
    if(next.isNull()){if(error)*error="Invalid GRANULAR grid dimensions";return false;}
    next.set("enabled",document["enabled"]);
    const std::size_t oldWidth=std::size_t(document["width"].integer());
    const std::size_t oldHeight=std::size_t(document["height"].integer());
    auto& newMass=next.member("mass");auto& newSolid=next.member("solid");
    for(std::size_t y=0;y<std::min(oldHeight,std::size_t(height));++y)
        for(std::size_t x=0;x<std::min(oldWidth,std::size_t(width));++x){
            newMass.at(y*width+x)=document["mass"][y*oldWidth+x];
            newSolid.at(y*width+x)=document["solid"][y*oldWidth+x];
        }
    if(!validate(next,error))return false;
    document=std::move(next);return true;
}
inline bool paint(Json& document,std::uint16_t x,std::uint16_t y,
                  I mass,bool blocked,std::string* error=nullptr){
    if(!validate(document,error))return false;
    const auto width=std::uint16_t(document["width"].integer());
    const auto height=std::uint16_t(document["height"].integer());
    if(x>=width||y>=height||mass<0||
       mass>document["capacity"].integer()||(blocked&&mass!=0)){
        if(error)*error="Invalid GRANULAR brush value or coordinate";
        return false;
    }
    Json next=document;
    const auto i=std::size_t(y)*width+x;
    next.member("mass").at(i)=Json::number(mass);
    next.member("solid").at(i)=Json::number(I(blocked?1:0));
    if(!validate(next,error))return false;
    document=std::move(next);return true;
}
inline bool apply(World& world,Entity e,const Json& doc,std::string* error=nullptr){
    if(!world.alive(e)||world.has<Field>(e)||!validate(doc,error))return false;
    Field f;
    f.width=std::uint16_t(doc["width"].integer());
    f.height=std::uint16_t(doc["height"].integer());
    f.capacity=doc["capacity"].integer();
    f.enabled=doc["enabled"].boolean();
    f.mass.clear();f.solid.clear();
    f.mass.reserve(doc["mass"].size());
    f.solid.reserve(doc["solid"].size());
    for(const auto& cell:doc["mass"].items())f.mass.push_back(cell.integer());
    for(const auto& cell:doc["solid"].items())f.solid.push_back(std::uint8_t(cell.integer()));
    if(!valid(f))return false;
    world.add<Field>(e,std::move(f));return true;
}
} // namespace aiverse::granular::authoring
