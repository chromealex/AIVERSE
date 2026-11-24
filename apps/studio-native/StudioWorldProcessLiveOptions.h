#pragma once
#include "engine/data/Json.h"

namespace aiverse::studio {
// Transient Studio settings only. The RPC patch is validated on a copy;
// no authored map, ECS state or persistence is affected.
struct StudioWorldProcessLiveOptions {
    std::string layer="combined";
    bool live=false;
    int32_t granularCellCm=0;
    bool gravity=false;
    std::string aetherLayer="off";
    bool fracture=false, morph=false, echo=false, crowd=false, circuit=false, society=false, forest=false;

    bool anyPlanetLayer() const {
        return layer!="off"||gravity||aetherLayer!="off"||fracture||morph||echo||crowd||circuit||society||forest;
    }
    bool operator==(const StudioWorldProcessLiveOptions&) const = default;
    struct Flag { const char* key; bool StudioWorldProcessLiveOptions::*member; };
    static constexpr Flag flags[]={
        {"live",&StudioWorldProcessLiveOptions::live},
        {"liveGravity",&StudioWorldProcessLiveOptions::gravity},
        {"liveFracture",&StudioWorldProcessLiveOptions::fracture},
        {"liveMorph",&StudioWorldProcessLiveOptions::morph},
        {"liveEcho",&StudioWorldProcessLiveOptions::echo},
        {"liveCrowd",&StudioWorldProcessLiveOptions::crowd},
        {"liveCircuit",&StudioWorldProcessLiveOptions::circuit},
        {"liveSociety",&StudioWorldProcessLiveOptions::society},
        {"liveForest",&StudioWorldProcessLiveOptions::forest}
    };
    StudioWorldProcessLiveOptions patched(const Json& params) const {
        auto next=*this;
        if(params.has("layer")){
            if(!params["layer"].isString())throw std::runtime_error("EVOLVE layer must be a string");
            const auto v=params.getStr("layer");
            if(v!="combined"&&v!="fertility"&&v!="pollution"&&v!="succession"&&v!="durability"&&v!="off")
                throw std::runtime_error("Invalid EVOLVE layer");
            next.layer=v;
        }
        if(params.has("granularCellCm")){
            const auto& v=params["granularCellCm"];
            const auto n=v.integer(-1);
            if(!v.isNumber()||n<0||n>10000||(n>0&&n<10)||v.fx()!=Fixed::fromInt(n))
                throw std::runtime_error("GRANULAR diagnostic cell size must be 0 or integer 10..10000 cm");
            next.granularCellCm=int32_t(n);
        }
        if(params.has("aetherLayer")){
            if(!params["aetherLayer"].isString())throw std::runtime_error("AETHER layer must be a string");
            const auto v=params.getStr("aetherLayer");
            if(v!="off"&&v!="smoke"&&v!="toxin"&&v!="oxygen")throw std::runtime_error("Invalid AETHER layer");
            next.aetherLayer=v;
        }
        for(const auto& flag:flags)if(params.has(flag.key)){
            if(params[flag.key].type()!=Json::Type::Bool)
                throw std::runtime_error(std::string(flag.key)+" must be boolean");
            next.*(flag.member)=params.getBool(flag.key);
        }
        return next;
    }
    Json json() const {
        auto out=Json::object();
        out.set("layer",Json::string(layer));
        out.set("granularCellCm",Json::number(int64_t(granularCellCm)));
        out.set("aetherLayer",Json::string(aetherLayer));
        for(const auto& flag:flags)out.set(flag.key,Json::fromBool(this->*(flag.member)));
        return out;
    }
};
}
