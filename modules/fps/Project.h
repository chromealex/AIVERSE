#pragma once
#include "engine/data/Json.h"
#include "engine/data/JsonCompose.h"
#include <functional>

namespace aiverse::fps {
struct Project { Json manifest,rules,map;std::string mapFile; };
using Reader=std::function<bool(const std::string&,std::string&)>;
inline Project readProject(const Reader& read) {
    Project p;std::string text;
    auto safe=[](const std::string& path){return !path.empty()&&path.front()!='/'&&path.find("..")==std::string::npos&&path.find('\\')==std::string::npos&&path.find(':')==std::string::npos;};
    if(read("game.json",text)) {
        p.manifest=Json::parse(text);
        if(p.manifest.getStr("module")!="fps")throw JsonError("game.module: expected fps");
        p.mapFile=p.manifest["maps"][0].getStr("file");
    } else p.mapFile="map.json"; // low-level regression fixtures
    auto rulesFile=p.manifest.getStr("rules","rules.json");
    if(!safe(rulesFile)||!safe(p.mapFile))throw JsonError("fps project: invalid relative content path");
    p.rules=readComposedJson(rulesFile,[&](const std::string& f,std::string& o){return read(f,o);});   // + includes / extends
    if(!read(p.mapFile,text))throw JsonError("fps project: cannot read "+p.mapFile);
    p.map=Json::parse(text);
    return p;
}
}
