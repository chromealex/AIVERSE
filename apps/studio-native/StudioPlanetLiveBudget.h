#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace aiverse::studio {
// Presentation-only budgets: independent of authoritative entity counts.
// Compact snapshot budget excludes the enclosing RPC message.
inline constexpr size_t kPlanetLivePayloadBytes=768*1024;
inline constexpr size_t kPlanetLiveRootReserve=4096;
inline constexpr size_t kPlanetLiveLines=4096;
inline constexpr size_t kPlanetLiveLayerCount=11;

inline size_t planetLiveShare(size_t total,size_t layers){
    return total/std::clamp(layers,size_t(1),kPlanetLiveLayerCount);
}
// Maintain a useful minimum for individual owners while reducing aggregate
// work as multiple layers are enabled. Existing owner safety caps remain.
inline size_t planetLiveOwnerLimit(size_t cap,size_t layers){
    return cap?std::max(size_t(1),planetLiveShare(cap,layers)):0;
}

struct PlanetLiveLineBudget {
    size_t remaining, layerRemaining=0, dropped=0;
    explicit PlanetLiveLineBudget(size_t total=kPlanetLiveLines):remaining(total){}
    void beginLayer(size_t share){layerRemaining=std::min(remaining,share);}
    bool take(){
        if(!remaining||!layerRemaining){++dropped;return false;}
        --remaining;--layerRemaining;return true;
    }
};

// Keep complete deterministic prefixes of each top-level row array, with
// round-robin allocation across row families. Owner summary counters continue
// to describe the original sampled candidates, not just transmitted rows.
inline bool fitPlanetLiveLayer(Json& layer,size_t bytes){
    if(layer.dump(-1).size()<=bytes)return true;
    if(!layer.isObject())return false;
    layer.set("truncated",Json::fromBool(true));
    layer.set("payloadTruncated",Json::fromBool(true));
    struct Rows { Json* target; std::vector<Json> original; size_t cursor=0; bool blocked=false; };
    std::vector<Rows> lists;
    for(auto& member:layer.membersMut())if(member.second.isArray()){
        auto& rows=member.second.itemsMut();
        lists.push_back({&member.second,{}});
        lists.back().original.swap(rows);
    }
    size_t used=layer.dump(-1).size();
    if(used>bytes)return false;
    bool progress=true;
    while(progress){
        progress=false;
        for(auto& list:lists){
            if(list.blocked||list.cursor==list.original.size())continue;
            const auto rowBytes=list.original[list.cursor].dump(-1).size()+(list.cursor?1:0);
            if(rowBytes>bytes-used){list.blocked=true;continue;}
            used+=rowBytes;
            list.target->push(std::move(list.original[list.cursor++]));
            progress=true;
        }
    }
    // Exact final check also protects against changes to Json formatting.
    return layer.dump(-1).size()<=bytes;
}
struct PlanetLivePayloadBudget {
    size_t layers;
    Json trimmed=Json::array(), omitted=Json::array();
    explicit PlanetLivePayloadBudget(size_t n):layers(std::clamp(n,size_t(1),kPlanetLiveLayerCount)){}
    size_t ownerLimit(size_t cap)const{return planetLiveOwnerLimit(cap,layers);}
    void add(Json& out,const char* key,Json layer){
        const size_t share=planetLiveShare(kPlanetLivePayloadBytes-kPlanetLiveRootReserve,layers);
        if(!fitPlanetLiveLayer(layer,share)){omitted.push(Json::string(key));return;}
        if(layer.getBool("payloadTruncated"))trimmed.push(Json::string(key));
        out.set(key,std::move(layer));
    }
    void finish(Json& out){
        Json meta=Json::object();
        meta.set("payloadLimitBytes",Json::number(int64_t(kPlanetLivePayloadBytes)));
        meta.set("lineLimit",Json::number(int64_t(kPlanetLiveLines)));
        meta.set("sampleLayers",Json::number(int64_t(layers)));
        meta.set("trimmedLayers",std::move(trimmed));
        meta.set("omittedLayers",std::move(omitted));
        out.set("diagnosticBudget",std::move(meta));
    }
};
}
