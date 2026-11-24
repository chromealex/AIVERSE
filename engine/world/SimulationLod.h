#pragma once
#include "engine/world/SpatialWorld.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace aiverse::world {
// Scheduling policy for work that the owning system explicitly declares safe
// to run less frequently (AI decisions, offscreen planning, etc.). Never use
// this to skip authoritative physics, collision, damage, ownership or WAL.
enum class SimulationLodTier : uint8_t { Hot, Warm, Cold };
struct SimulationLodConfig {
    bool enabled=false;
    uint32_t hotCells=4,warmCells=16,warmTicks=4,coldTicks=16;
    bool valid()const noexcept{
        return hotCells<=warmCells&&warmCells<=4096&&
            warmTicks>=1&&warmTicks<=coldTicks&&coldTicks<=120;
    }
};
inline SimulationLodConfig readSimulationLod(const Json& j){
    SimulationLodConfig c;
    if(j.isNull())return c;
    if(!j.isObject())throw JsonError("simulationLod: expected object");
    for(const auto& [key,value]:j.members()){
        (void)value;
        if(key!="enabled"&&key!="hotCells"&&key!="warmCells"&&key!="warmTicks"&&key!="coldTicks")
            throw JsonError("simulationLod: unknown key "+key);
    }
    if(j.has("enabled")&&j["enabled"].type()!=Json::Type::Bool)
        throw JsonError("simulationLod.enabled: expected boolean");
    c.enabled=j.getBool("enabled",true);
    auto read=[&](const char* key,uint32_t fallback,uint32_t max){
        if(!j.has(key))return fallback;
        auto n=j[key].integer(-1);
        if(!j[key].isNumber()||n<0||uint64_t(n)>max||j[key].fx()!=Fixed::fromInt(n))
            throw JsonError(std::string("simulationLod.")+key+": invalid integer");
        return uint32_t(n);
    };
    c.hotCells=read("hotCells",c.hotCells,4096);
    c.warmCells=read("warmCells",c.warmCells,4096);
    c.warmTicks=read("warmTicks",c.warmTicks,120);
    c.coldTicks=read("coldTicks",c.coldTicks,120);
    if(!c.valid())throw JsonError("simulationLod: require hotCells <= warmCells and 1 <= warmTicks <= coldTicks <= 120");
    return c;
}
inline uint64_t cellDistance(Cell a,Cell b)noexcept{
    auto delta=[](int32_t x,int32_t y){const int64_t d=int64_t(x)-int64_t(y);return uint64_t(d<0?-d:d);};
    return std::max({delta(a.x,b.x),delta(a.y,b.y),delta(a.z,b.z)});
}
// Derived diagnostic counters from the last decision pass, not gameplay state.
struct SimulationLodDiagnostics {
    uint32_t tick=0,hot=0,warm=0,cold=0,executed=0,deferred=0;
};
inline Json simulationLodConfigJson(const SimulationLodConfig& c){
    Json j=Json::object();j.set("enabled",Json::fromBool(c.enabled));
    j.set("hotCells",Json::number(int64_t(c.hotCells)));j.set("warmCells",Json::number(int64_t(c.warmCells)));
    j.set("warmTicks",Json::number(int64_t(c.warmTicks)));j.set("coldTicks",Json::number(int64_t(c.coldTicks)));
    return j;
}
inline Json simulationLodDiagnosticsJson(const SimulationLodDiagnostics* d){
    Json j=Json::object();j.set("available",Json::fromBool(d!=nullptr));if(!d)return j;
    j.set("tick",Json::number(int64_t(d->tick)));j.set("hot",Json::number(int64_t(d->hot)));
    j.set("warm",Json::number(int64_t(d->warm)));j.set("cold",Json::number(int64_t(d->cold)));
    j.set("executed",Json::number(int64_t(d->executed)));j.set("deferred",Json::number(int64_t(d->deferred)));return j;
}
struct SimulationLodDecision {
    SimulationLodTier tier=SimulationLodTier::Hot;
    uint32_t period=1;
    bool due=true;
};
// Use authoritative simulation observers, never render cameras or the
// asynchronously loaded asset set. An absent observer/identity fails hot.
// No independent clock or RNG: reconstruct exactly after save and rollback.
inline SimulationLodDecision simulationLodDecision(const SimulationLodConfig& c,
    Cell position,std::span<const Cell> observers,uint64_t stableId,uint32_t tick,
    bool critical=false)noexcept{
    if(!c.enabled||!c.valid()||critical||!stableId||observers.empty()||tick==0)return {};
    uint64_t distance=std::numeric_limits<uint64_t>::max();
    for(auto focus:observers){
        distance=std::min(distance,cellDistance(position,focus));
        if(distance<=c.hotCells)return {};
    }
    const bool warm=distance<=c.warmCells;
    const auto period=warm?c.warmTicks:c.coldTicks;
    return {warm?SimulationLodTier::Warm:SimulationLodTier::Cold,period,
        tick%period==stableId%period};
}
// Deterministic fair round-robin batch size for owners that already persist
// their own cursor (e.g. virtual population). No global state or new identity.
inline size_t simulationSliceForWindow(size_t count,uint32_t window)noexcept{
    if(!count||!window)return 0;
    return count/window+(count%window!=0);
}
}
