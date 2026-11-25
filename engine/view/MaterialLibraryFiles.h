#pragma once
#include "engine/data/Json.h"
#include <stdexcept>
#include <string>

namespace aiverse::view {
// Legacy entries are inline objects. File-backed entries are project-relative
// content paths, so the same index works in a workspace and a packed game.
inline bool materialAssetPath(const std::string& path) {
    if (!path.starts_with("content/") || !path.ends_with(".material.json") ||
        path.size()>512 || path.find_first_of("\\:")!=std::string::npos) return false;
    for(size_t start=0;start<path.size();) {
        const auto end=path.find('/',start);
        const auto part=path.substr(start,end-start);
        if(part.empty() || part=="." || part=="..")return false;
        if(end==std::string::npos)break;
        start=end+1;
    }
    return true;
}

template<class Read>
Json materialLibraryFromIndex(const Json& index, Read read) {
    if(!index["materials"].isObject())throw std::runtime_error("Missing materials object");
    Json materials=index["materials"];
    for(const auto& [name,entry]:index["materials"].members()) {
        if(!entry.isString())continue;
        const auto path=entry.str();
        std::string text;
        if(!materialAssetPath(path)||!read(path,text))throw std::runtime_error("Cannot load material "+name+" from "+path);
        Json material=Json::parse(text);
        if(!material.isObject())throw std::runtime_error("Invalid material file: "+path);
        materials.set(name,std::move(material));
    }
    return materials;
}
}
