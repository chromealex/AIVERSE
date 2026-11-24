#pragma once
// Reads a voxel game project: game.json ("module": "voxel"), rules (with includes / extends) and the first map.
#include "engine/data/Json.h"
#include "engine/data/JsonCompose.h"
#include <functional>
namespace aiverse::voxelgame {
struct Project{Json manifest,rules,map;};
using Reader=std::function<bool(const std::string&,std::string&)>;
inline Project readProject(const Reader& read){
    Project p;std::string text;
    auto safe=[](const std::string& path){return !path.empty()&&path.front()!='/'&&path.find("..")==std::string::npos&&path.find('\\')==std::string::npos&&path.find(':')==std::string::npos;};
    if(!read("game.json",text))throw JsonError("voxel project: game.json not found");
    p.manifest=Json::parse(text);if(p.manifest.getStr("module")!="voxel")throw JsonError("game.module: expected voxel");
    auto rulesFile=p.manifest.getStr("rules","rules.json"),mapFile=p.manifest["maps"][0].getStr("file");
    if(!safe(rulesFile)||!safe(mapFile))throw JsonError("voxel project: invalid relative content path");
    p.rules=readComposedJson(rulesFile,[&](const std::string& f,std::string& o){return read(f,o);});
    if(!read(mapFile,text))throw JsonError("voxel project: cannot read "+mapFile);
    p.map=Json::parse(text);return p;
}
}
