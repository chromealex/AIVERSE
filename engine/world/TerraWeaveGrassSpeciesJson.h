// JSON authoring for TerraWeave meadow grass (species, clumps, fields).
//
//   "meadow": "dryMeadow"                       // a preset name, or
//   "meadow": {
//     "preset": "meadow",                       // optional base (default: empty)
//     "seed": 7,                                // world seed for clumps/fields
//     "species": [                              // replaces the preset's species
//       {"preset": "meadowFine", "weightPermille": 600, "heightCm": 45},
//       {"preset": "tallDry", "weightPermille": 250},
//       {"name": "myReed", "heightCm": 90, "halfWidthMm": 7, "tipColor": [190,180,120],
//        "head": "spike", "headPermille": 300}
//     ],
//     "clumps": {"spacingCm": 55, "radiusCm": 22, "gapPermille": 250},
//     "field":  {"periodCm": 2500, "dryPatchPermille": 300}
//   }
//
// Absent block (or "legacy") = legacy uniform grass, unchanged. Unknown keys,
// presets and out-of-range values throw readable std::invalid_argument errors.
#pragma once
#include "engine/data/Json.h"
#include "engine/world/TerraWeaveGrassSpecies.h"
#include <set>
#include <stdexcept>
#include <string>

namespace aiverse::terraweave {

namespace grassjson {
inline void rejectUnknown(const Json& j,const std::set<std::string>& known,const std::string& where) {
    for(const auto& [key,value]:j.members()){
        (void)value;
        if(!known.contains(key))throw std::invalid_argument(where+": unknown key '"+key+"'");
    }
}
inline void readU32(const Json& j,const char* key,uint32_t& out,const std::string& where) {
    if(!j.has(key))return;
    const auto& v=j[key];
    if(!v.isNumber())throw std::invalid_argument(where+": '"+key+"' must be a number");
    const auto n=v.integer();
    if(n<0||n>100000)throw std::invalid_argument(where+": '"+key+"' out of range");
    out=uint32_t(n);
}
inline void readRgb(const Json& j,const char* key,GrassRgb& out,const std::string& where) {
    if(!j.has(key))return;
    const auto& v=j[key];
    if(!v.isArray()||v.size()!=3)throw std::invalid_argument(where+": '"+key+"' must be [r,g,b] 0..255");
    int c[3];
    for(size_t i=0;i<3;++i){
        if(!v[i].isNumber()||v[i].integer()<0||v[i].integer()>255)
            throw std::invalid_argument(where+": '"+key+"' must be [r,g,b] 0..255");
        c[i]=int(v[i].integer());
    }
    out={uint8_t(c[0]),uint8_t(c[1]),uint8_t(c[2])};
}
inline GrassHead head(const std::string& name,const std::string& where) {
    if(name=="none")return GrassHead::None;
    if(name=="panicle")return GrassHead::Panicle;
    if(name=="spike")return GrassHead::Spike;
    if(name=="flower")return GrassHead::Flower;
    throw std::invalid_argument(where+": unknown head '"+name+"' (none|panicle|spike|flower)");
}
}

inline GrassSpecies parseGrassSpecies(const Json& j,const std::string& where) {
    using namespace grassjson;
    if(!j.isObject())throw std::invalid_argument(where+": species must be an object");
    rejectUnknown(j,{"preset","name","weightPermille","heightCm","heightVariationPermille","halfWidthMm",
        "widthVariationPermille","curvaturePermille","droopPermille","leanPermille","twistPermille",
        "taperPermille","shoulderPermille","segments","rootColor","midColor","tipColor",
        "colorJitterPermille","dryPermille","dryColor","head","headPermille","stemHeightPermille",
        "headLengthMm","headHalfWidthMm","headColor","fieldAffinityPermille","fieldTolerancePermille",
        "flexPermille"},where);
    GrassSpecies s;
    if(j.has("preset")){
        const auto name=j["preset"].str();
        auto preset=grassSpeciesPreset(name);
        if(!preset)throw std::invalid_argument(where+": unknown species preset '"+name+"'");
        s=*preset;
    }
    if(j.has("name"))s.name=j["name"].str(s.name);
    readU32(j,"weightPermille",s.weightPermille,where);
    readU32(j,"heightCm",s.heightCm,where);
    readU32(j,"heightVariationPermille",s.heightVariationPermille,where);
    readU32(j,"halfWidthMm",s.halfWidthMm,where);
    readU32(j,"widthVariationPermille",s.widthVariationPermille,where);
    readU32(j,"curvaturePermille",s.curvaturePermille,where);
    readU32(j,"droopPermille",s.droopPermille,where);
    readU32(j,"leanPermille",s.leanPermille,where);
    readU32(j,"twistPermille",s.twistPermille,where);
    readU32(j,"taperPermille",s.taperPermille,where);
    readU32(j,"shoulderPermille",s.shoulderPermille,where);
    readU32(j,"segments",s.segments,where);
    readRgb(j,"rootColor",s.rootColor,where);
    readRgb(j,"midColor",s.midColor,where);
    readRgb(j,"tipColor",s.tipColor,where);
    readU32(j,"colorJitterPermille",s.colorJitterPermille,where);
    readU32(j,"dryPermille",s.dryPermille,where);
    readRgb(j,"dryColor",s.dryColor,where);
    if(j.has("head"))s.head=head(j["head"].str(),where);
    readU32(j,"headPermille",s.headPermille,where);
    readU32(j,"stemHeightPermille",s.stemHeightPermille,where);
    readU32(j,"headLengthMm",s.headLengthMm,where);
    readU32(j,"headHalfWidthMm",s.headHalfWidthMm,where);
    readRgb(j,"headColor",s.headColor,where);
    readU32(j,"fieldAffinityPermille",s.fieldAffinityPermille,where);
    readU32(j,"fieldTolerancePermille",s.fieldTolerancePermille,where);
    readU32(j,"flexPermille",s.flexPermille,where);
    return s;
}

// Parses a "meadow" value (string preset or object). Validates the result.
inline GrassMeadow parseGrassMeadow(const Json& j,const std::string& where="grass meadow") {
    using namespace grassjson;
    GrassMeadow m;
    if(j.isNull())return m;
    if(j.isString()){
        auto preset=grassMeadowPreset(j.str());
        if(!preset)throw std::invalid_argument(where+": unknown meadow preset '"+j.str()+"'");
        m=*preset;
        validateGrassMeadow(m);
        return m;
    }
    if(!j.isObject())throw std::invalid_argument(where+": expected a preset name or an object");
    rejectUnknown(j,{"preset","seed","species","clumps","field"},where);
    if(j.has("preset")){
        auto preset=grassMeadowPreset(j["preset"].str());
        if(!preset)throw std::invalid_argument(where+": unknown meadow preset '"+j["preset"].str()+"'");
        m=*preset;
    }
    if(j.has("seed")){
        if(!j["seed"].isNumber())throw std::invalid_argument(where+": 'seed' must be a number");
        m.worldSeed=uint64_t(j["seed"].integer());
    }
    if(j.has("species")){
        const auto& list=j["species"];
        if(!list.isArray())throw std::invalid_argument(where+": 'species' must be an array");
        if(list.size()>kMaxGrassSpecies)throw std::invalid_argument(where+": at most 8 species");
        m.species.clear();
        for(size_t i=0;i<list.size();++i)
            m.species.push_back(parseGrassSpecies(list[i],where+": species["+std::to_string(i)+"]"));
    }
    if(j.has("clumps")){
        const auto& c=j["clumps"];const auto w=where+": clumps";
        if(!c.isObject())throw std::invalid_argument(w+" must be an object");
        rejectUnknown(c,{"spacingCm","radiusCm","radiusVariationPermille","clumpedPermille","gapPermille",
            "mixPermille","centreDensityPermille","fillHeightPermille"},w);
        readU32(c,"spacingCm",m.clumps.spacingCm,w);
        readU32(c,"radiusCm",m.clumps.radiusCm,w);
        readU32(c,"radiusVariationPermille",m.clumps.radiusVariationPermille,w);
        readU32(c,"clumpedPermille",m.clumps.clumpedPermille,w);
        readU32(c,"gapPermille",m.clumps.gapPermille,w);
        readU32(c,"mixPermille",m.clumps.mixPermille,w);
        readU32(c,"centreDensityPermille",m.clumps.centreDensityPermille,w);
        readU32(c,"fillHeightPermille",m.clumps.fillHeightPermille,w);
    }
    if(j.has("field")){
        const auto& f=j["field"];const auto w=where+": field";
        if(!f.isObject())throw std::invalid_argument(w+" must be an object");
        rejectUnknown(f,{"periodCm","densityContrastPermille","heightContrastPermille","dryPatchPermille"},w);
        readU32(f,"periodCm",m.field.periodCm,w);
        readU32(f,"densityContrastPermille",m.field.densityContrastPermille,w);
        readU32(f,"heightContrastPermille",m.field.heightContrastPermille,w);
        readU32(f,"dryPatchPermille",m.field.dryPatchPermille,w);
    }
    try{validateGrassMeadow(m);}
    catch(const std::invalid_argument& e){throw std::invalid_argument(where+": "+e.what());}
    return m;
}

} // namespace aiverse::terraweave
