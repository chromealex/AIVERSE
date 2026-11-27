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
    // authoredWaterDocument projects only water entities, preserving map order.
    // Map entity IDs are optional and need not distinguish these records.
    size_t waterIndex=0;
    const auto& edited=candidate["entities"];
    for(auto& entity:entities) {
        if(!entity.extra["components"].has("NereidWater"))continue;
        if(waterIndex>=edited.size())throw JsonError("Water entity disappeared");
        entity.extra.set("components",edited[waterIndex++]["components"]);
    }
    if(waterIndex!=edited.size())throw JsonError("Water entity count changed");
    auto extra=map.data.extra;
    if(candidate.has("nereidWater"))extra.set("nereidWater",candidate["nereidWater"]);
    map.beginEdit();map.data.entities=std::move(entities);map.data.extra=std::move(extra);map.touched();
}
} // namespace aiverse::editor
