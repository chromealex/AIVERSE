#pragma once
#include "../data/Json.h"
#include <cmath>
#include <stdexcept>
namespace aiverse::render {
// Versioned presentation settings, shared by native hosts and authoring tools.
// No gameplay authority, GPU handles, or genre-specific serialization.
inline Json renderLightingSettings(const Json& input) {
    Json result=Json::parse(R"({"version":1,"indirect":"off","direct":"legacy","quality":"high","probes":8,"layers":4,"spacing":4,"samples":32,"distance":32,"cpuBudgetMs":2})");
    if(input.isNull())return result;
    if(!input.isObject())throw std::invalid_argument("renderLighting must be an object");
    for(const auto& [key,value]:input.members()) {
        if(!result.has(key))throw std::invalid_argument("Unknown renderLighting field: "+key);
        if(key=="indirect" || key=="direct" || key=="quality") {
            if(!value.isString())throw std::invalid_argument("Lighting mode must be text");
            const auto mode=value.str();
            const bool valid=key=="indirect"?(mode=="off"||mode=="cpu"||mode=="auto"||mode=="gpu"):
                key=="direct"?(mode=="legacy"||mode=="luma"):(mode=="balanced"||mode=="high"||mode=="ultra");
            if(!valid)throw std::invalid_argument("Unsupported lighting mode: "+key);
        } else {
            if(!value.isNumber()||!std::isfinite(value.toDouble()))throw std::invalid_argument("Lighting value must be finite");
            const double n=value.toDouble();
            bool valid=false;
            if(key=="version")valid=n==1;
            else if(key=="probes")valid=n>=4&&n<=16&&n==std::floor(n);
            else if(key=="layers")valid=n>=2&&n<=8&&n==std::floor(n);
            else if(key=="samples")valid=n>=8&&n<=64&&n==std::floor(n);
            else if(key=="spacing")valid=n>=.25&&n<=64;
            else if(key=="distance")valid=n>=1&&n<=256;
            else if(key=="cpuBudgetMs")valid=n>=.1&&n<=20;
            if(!valid)throw std::invalid_argument("Lighting value out of range: "+key);
        }
        result.set(key,value);
    }
    return result;
}
}
