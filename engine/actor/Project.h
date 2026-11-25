#pragma once
#include "engine/data/Json.h"
#include "engine/data/JsonCompose.h"
#include <functional>

namespace aiverse::actor {
struct Project { Json manifest,rules,map;std::string mapFile; };
using Reader=std::function<bool(const std::string&,std::string&)>;
inline Project readProject(const Reader& read, size_t mapIndex = 0) {
    Project p;std::string text;
    auto safe=[](const std::string& path){return !path.empty()&&path.front()!='/'&&path.find("..")==std::string::npos&&path.find('\\')==std::string::npos&&path.find(':')==std::string::npos;};
    if(read("game.json",text)) {
        p.manifest=Json::parse(text);
        if(p.manifest.getStr("module")!="actor")throw JsonError("game.module: expected actor");
        if (mapIndex >= p.manifest["maps"].size()) throw JsonError("actor project: map index out of range");
        p.mapFile=p.manifest["maps"][mapIndex].getStr("file");
    } else p.mapFile="map.json"; // low-level regression fixtures
    auto rulesFile=p.manifest.getStr("rules","rules.json");
    if(!safe(rulesFile)||!safe(p.mapFile))throw JsonError("actor project: invalid relative content path");
    p.rules=readComposedJson(rulesFile,[&](const std::string& f,std::string& o){return read(f,o);});   // + includes / extends
    if(!read(p.mapFile,text))throw JsonError("actor project: cannot read "+p.mapFile);
    p.map=Json::parse(text);
    return p;
}
}
