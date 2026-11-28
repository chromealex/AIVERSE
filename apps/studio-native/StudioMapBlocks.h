#pragma once
#include "../editor/core/MapDoc.h"

namespace aiverse::studio {
inline bool inspectableMapBlock(const editor::MapData& map,const std::string& key){
    return !map.worldSpace && !key.empty() && key.front()!='$' && key!="areas" && key!="waves" &&
           key!="triggers" && key!="victory" && map.extra.has(key);
}
inline size_t mapBlockEntryCount(const Json& value){
    return value.isArray() && value.size()<=200 && value[0].isObject() ? value.size() : 0;
}
inline Json mapBlockEntryNames(const std::string& key,const Json& value){
    Json names=Json::array();
    for(size_t i=0;i<mapBlockEntryCount(value);++i)
        names.push(Json::string(value[i].getStr("id",value[i].getStr("name",key+"["+std::to_string(i)+"]"))));
    return names;
}
}
