// Optional authoring policy for pooled scene emissions. Explicit opt-in:
 // a looping legacy EffectDef is never silently reinterpreted as bursts.
#pragma once
#include "engine/fx/SpectraGpuPoolWorld.h"
#include <cmath>
#include <stdexcept>
namespace aiverse::spectra {
inline bool readScenePoolEmission(const Json& graph,PoolContinuousEmission& out){
    const auto& p=graph["scenePoolEmission"];
    if(p.isNull())return false;
    if(!p.isObject())throw std::runtime_error("scenePoolEmission must be an object");
    auto read=[&](const char* key,double max,double fallback){
        if(!p.has(key))return fallback;
        const auto& v=p[key];const double n=v.toDouble();
        if(!v.isNumber()||!std::isfinite(n)||n<0||n>max)
            throw std::runtime_error(std::string("Invalid scenePoolEmission.")+key);
        return n;
    };
    PoolContinuousEmission candidate;
    candidate.burstsPerSecond=float(read("burstsPerSecond",120,0));
    candidate.metresPerBurst=float(read("metresPerBurst",100000,0));
    candidate.lifetimeSeconds=float(read("lifetimeSeconds",86400,0));
    const auto max=read("maxBurstsPerFrame",64,8);
    if(max<1||std::floor(max)!=max||
       (candidate.burstsPerSecond==0&&candidate.metresPerBurst==0))
        throw std::runtime_error("scenePoolEmission requires a positive emission rate and integral burst budget");
    candidate.maxBurstsPerFrame=uint32_t(max);
    out=candidate;return true;
}
}
