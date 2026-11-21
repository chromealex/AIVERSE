// Presentation only. PHYSIS remains the owner of authoritative weather.
#pragma once
#include "Math3D.h"
#include "../data/Json.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aiverse::render {
struct PostFrame;
struct PhysicalSkySettings {
    bool enabled=false, clouds=true, godRays=true;
    float metersPerUnit=1, seaLevel=0;
    float rayleigh=1, mie=1, sunRadiance=20;
    float cloudBase=1500, cloudThickness=1500, cloudScale=2400;
    float coverage=.45f, extinction=.003f, anisotropy=.65f;
    float windX=12, windZ=4, haze=.00001f, rayDistance=8000;
};
struct PhysicalSkyBudget { int atmosphereSteps, cloudSteps, lightSteps, raySteps; };
inline PhysicalSkyBudget physicalSkyBudget(int quality) {
    if(quality<=0)return {};
    if(quality==1)return {8,24,3,8};
    if(quality==2)return {12,48,4,16};
    return {20,96,6,32};
}
inline PhysicalSkySettings sanitizePhysicalSky(PhysicalSkySettings s) {
    const PhysicalSkySettings d;
    auto finite=[](float v,float fallback,float lo,float hi) {
        return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
    };
#define SKY_LIMIT(field,lo,hi) s.field=finite(s.field,d.field,lo,hi)
    SKY_LIMIT(metersPerUnit,.001f,1000.f); SKY_LIMIT(seaLevel,-100000.f,100000.f);
    SKY_LIMIT(rayleigh,0.f,8.f); SKY_LIMIT(mie,0.f,8.f); SKY_LIMIT(sunRadiance,0.f,100.f);
    SKY_LIMIT(cloudBase,100.f,15000.f); SKY_LIMIT(cloudThickness,100.f,8000.f);
    SKY_LIMIT(cloudScale,100.f,50000.f); SKY_LIMIT(coverage,0.f,1.f);
    SKY_LIMIT(extinction,0.f,.1f); SKY_LIMIT(anisotropy,-.9f,.9f);
    SKY_LIMIT(windX,-200.f,200.f); SKY_LIMIT(windZ,-200.f,200.f);
    SKY_LIMIT(haze,0.f,.002f); SKY_LIMIT(rayDistance,1.f,50000.f);
#undef SKY_LIMIT
    return s;
}
// Each postfx.physicalSky block overlays the previous scope. Missing blocks reset
// to defaults on configure, preventing atmosphere leakage on project/map switches.
inline PhysicalSkySettings parsePhysicalSky(const Json& game,const Json& map,
                                            std::vector<std::string>* issues=nullptr) {
    PhysicalSkySettings s;
    for(const Json* scope:{&game,&map}) {
        const auto& j=(*scope)["physicalSky"];
        if(j.isNull())continue;
        if(!j.isObject()){if(issues)issues->push_back("physicalSky: expected object");continue;}
        for(const auto& [key,value]:j.members()) {
            bool known=false;
#define SKY_BOOL(field) if(key==#field){known=true;if(value.type()==Json::Type::Bool)s.field=value.boolean();else if(issues)issues->push_back("physicalSky."+key+": expected boolean");}
            SKY_BOOL(enabled) SKY_BOOL(clouds) SKY_BOOL(godRays)
#undef SKY_BOOL
#define SKY_FLOAT(field) if(key==#field){known=true;if(value.isNumber())s.field=float(value.toDouble());else if(issues)issues->push_back("physicalSky."+key+": expected number");}
            SKY_FLOAT(metersPerUnit) SKY_FLOAT(seaLevel) SKY_FLOAT(rayleigh) SKY_FLOAT(mie)
            SKY_FLOAT(sunRadiance) SKY_FLOAT(cloudBase) SKY_FLOAT(cloudThickness)
            SKY_FLOAT(cloudScale) SKY_FLOAT(coverage) SKY_FLOAT(extinction) SKY_FLOAT(anisotropy)
            SKY_FLOAT(windX) SKY_FLOAT(windZ) SKY_FLOAT(haze) SKY_FLOAT(rayDistance)
#undef SKY_FLOAT
            if(!known&&issues)issues->push_back("physicalSky: unknown key '"+key+"'");
        }
    }
    return sanitizePhysicalSky(s);
}
class PhysicalSky {
public:
    void shutdown(gfx::Device&);
    bool draw(gfx::Device&,const PostFrame&,const PhysicalSkySettings&,int quality,double seconds,
              gfx::TextureHandle source,gfx::TextureHandle depth,
              gfx::TextureHandle target,gfx::TextureHandle scratchDepth,gfx::BufferHandle triangle,
              const float2* displacementMeters=nullptr);
    const std::string& lastError()const{return error_;}
private:
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipe_;
    bool failed_=false;
    std::string error_;
};
}
