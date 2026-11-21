// Compact read-only water document view; canonical state remains MapDoc.
#pragma once
#include "apps/editor/AuthoringComponents.h"
#include "engine/world/NereidFlowAuthoring.h"
namespace aiverse::editor {
inline Json authoredWaterDocument(const MapDoc& map) {
    auto result=map.data.extra;auto entities=Json::array();
    for(const auto& e:map.data.entities)if(e.extra["components"].has("NereidWater")) {
        auto j=e.extra;auto p=Json::array();p.push(Json::number(double(e.x),6));p.push(Json::number(double(e.z),6));j.set("pos",p);j.set("id",Json::string(e.id));entities.push(j);
    }
    result.set("entities",entities);return result;
}
inline void editAuthoredWaterFlow(MapDoc& map,const Json& input) {
    const auto candidate=world::editNereidFlow(authoredWaterDocument(map),input);
    auto entities=map.data.entities;
    for(const auto& j:candidate["entities"].items()) {
        const int index=map.findEntity(j.getStr("id"));if(index<0)throw JsonError("Water entity disappeared");
        entities[size_t(index)].extra.set("components",j["components"]);
    }
    auto extra=map.data.extra;
    if(candidate.has("nereidWater"))extra.set("nereidWater",candidate["nereidWater"]);
    if(entities.size()!=map.data.entities.size())throw JsonError("Water entity count changed");
    map.beginEdit();map.data.entities=std::move(entities);map.data.extra=std::move(extra);map.touched();
}
} // namespace aiverse::editor
