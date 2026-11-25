#pragma once
// Read-only diagnostic DTO; no registration, persistence or graph evaluation.
#include "engine/buildings/BuildingGraphChanges.h"
#include "engine/data/Json.h"
#include <string>

namespace aiverse::building {
inline Json buildingGraphDiagnosticsJson(const BuildingGraphDiagnostics& d){
    Json out=Json::object();
    out.set("version",Json::number(int64_t(1)));
    const auto mask=[&](const char* name,uint32_t v){out.set(name,Json::number(int64_t(v)));};
    mask("registeredDomains",d.registeredDomains);mask("requestedDomains",d.requestedDomains);
    mask("pendingDomains",d.pendingDomains);mask("acknowledgedDomains",d.acknowledgedDomains);
    mask("missingOwners",d.missingOwners);mask("refusedDomains",d.refusedDomains);
    out.set("attempts",Json::string(std::to_string(d.attempts)));
    out.set("rebuildCompletionKnown",Json::fromBool(false));
    Json timings=Json::array();
    for(size_t i=0;i<d.callbackTimings.size();++i){
        const auto& t=d.callbackTimings[i];if(!t.calls)continue;
        Json row=Json::object();
        row.set("domain",Json::number(int64_t(1u<<i)));
        row.set("calls",Json::string(std::to_string(t.calls)));
        row.set("lastNs",Json::string(std::to_string(t.lastNs)));
        row.set("totalNs",Json::string(std::to_string(t.totalNs)));
        row.set("maxNs",Json::string(std::to_string(t.maxNs)));
        timings.push(std::move(row));
    }
    out.set("callbackTimings",std::move(timings));
    if(d.lastChange){
        const auto& c=*d.lastChange;
        Json change=Json::object();
        change.set("sceneId",Json::string(std::to_string(c.sceneId)));
        change.set("revision",Json::string(std::to_string(c.revision)));
        change.set("featureIndex",Json::number(int64_t(c.featureIndex)));
        change.set("kind",Json::number(int64_t(c.kind)));
        change.set("open",Json::fromBool(c.open));
        auto point=[](const auto& p){
            Json a=Json::array();
            a.push(Json::number(p.x.toDouble(),6));a.push(Json::number(p.y.toDouble(),6));a.push(Json::number(p.z.toDouble(),6));return a;
        };
        change.set("min",point(c.bounds.min));change.set("max",point(c.bounds.max));
        out.set("lastChange",std::move(change));
    }
    return out;
}
} // namespace aiverse::building
