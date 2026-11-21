#pragma once
#include "engine/data/Json.h"
#include <atomic>
#include <cstdint>
#include <chrono>
#include <set>
#include <string>

namespace aiverse::editor {
// Compatibility storage: lights[] remains the sole canonical component payload.
// entityId is editor identity, never an authoritative simulation/GPU handle.
inline void ensureDocumentLightIds(Json& document,bool legacyLoad=false) {
    if(!document["lights"].isArray())return;
    std::set<std::string> used;
    for(const auto& light:document["lights"].items()) {
        const auto id=light.getStr("entityId");
        if(!id.empty())used.insert(id);
    }
    std::set<std::string> seen;
    static std::atomic<uint64_t> sequence{0};
    for(size_t i=0;i<document["lights"].size();++i) {
        auto& light=document.member("lights").at(i);
        if(!light.isObject())continue;
        auto id=light.getStr("entityId");
        if(id.empty() || !seen.insert(id).second) {
            if(legacyLoad) {
                id="legacy-light-"+std::to_string(i);
                while(used.count(id))id+="-migrated";
            } else do {
                id="light-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+
                   "-"+std::to_string(++sequence);
            } while(used.count(id));
            used.insert(id);seen.insert(id);light.set("entityId",Json::string(id));
        }
    }
}
}
