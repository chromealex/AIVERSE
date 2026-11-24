// Read-only TerraWeave Data Interfaces. No new terrain generator.
#pragma once
#include "engine/fx/SpectraDataInterfaces.h"
#include "engine/world/TerraWeaveWorldHeightSource.h"
#include "engine/world/TerraWeaveSphere.h"
#include <memory>
namespace aiverse::world {
struct SpectraPlanetTerrain {
    render::float3 center{};
    double baseRadiusMeters=80;
    double heightScale=1;
    terraweave::SphereSettings sphere{};
    terraweave::TerrainSettings terrain{};
};
inline bool spectraTerrainChannel(std::string_view c,double relief,double clearance,
        render::float3 up,float& out) {
    double v=0;
    if(c=="height")v=relief;
    else if(c=="clearance")v=clearance;
    else if(c=="up_x")v=up.x;
    else if(c=="up_y")v=up.y;
    else if(c=="up_z")v=up.z;
    else return false;
    if(!std::isfinite(v)||std::abs(v)>1e7)return false;
    out=float(v);return true;
}
// Missing resident tiles are unavailable. Up is reference up, not slope normal.
inline bool publishSpectraTerrain(spectra::DataInterfaceRegistry& registry,
        std::shared_ptr<const terraweave::AuthoredWorldHeightSnapshot> snapshot,
        const std::string& name="terraweave") {
    if(!spectra::instanceParameterNameOk(name))return false;
    if(!snapshot){registry.unbind(name);return true;}
    return registry.bind(name,[snapshot=std::move(snapshot)](std::string_view c,render::float3 p,float& v){
        constexpr double limit=double(terraweave::kWorldLimitCm)/100.0;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
           std::abs(p.x)>limit||std::abs(p.z)>limit)return false;
        const auto height=snapshot->sampleQ16(
            int64_t(std::llround(double(p.x)*100*65536)),
            int64_t(std::llround(double(p.z)*100*65536)));
        if(!height)return false;
        const double metres=double(*height)/100;
        return spectraTerrainChannel(c,metres,double(p.y)-metres,{0,1,0},v);
    });
}
// Supply the same field radius, base radius and relief scale as the rendered planet.
inline bool publishSpectraPlanetTerrain(spectra::DataInterfaceRegistry& registry,
        SpectraPlanetTerrain config,const std::string& name="terraweave") {
    const auto& t=config.terrain;
    if(!spectra::instanceParameterNameOk(name)||t.basePeriodCm<=0||!t.octaves||t.octaves>12||t.reliefCm<0||
       !std::isfinite(config.center.x)||!std::isfinite(config.center.y)||!std::isfinite(config.center.z)||
       !std::isfinite(config.baseRadiusMeters)||config.baseRadiusMeters<=0||config.baseRadiusMeters>1e7||
       !std::isfinite(config.heightScale)||config.heightScale<0||config.heightScale>1000||
       config.sphere.radiusCm<=0||config.sphere.radiusCm>terraweave::kWorldLimitCm/2)return false;
    return registry.bind(name,[config](std::string_view c,render::float3 p,float& v){
        const double x=double(p.x)-config.center.x,y=double(p.y)-config.center.y,z=double(p.z)-config.center.z;
        const double radius=std::sqrt(x*x+y*y+z*z);
        if(!std::isfinite(radius)||radius<=1e-6||radius>1e7)return false;
        const render::float3 up{float(x/radius),float(y/radius),float(z/radius)};
        const auto point=terraweave::spherePoint(config.sphere,
            int64_t(std::llround(x/radius*65536)),int64_t(std::llround(y/radius*65536)),
            int64_t(std::llround(z/radius*65536)));
        const double relief=double(terraweave::heightAtSphere(config.terrain,point))*config.heightScale/100;
        const double surface=config.baseRadiusMeters+relief;
        if(surface<=0)return false;
        return spectraTerrainChannel(c,relief,radius-surface,up,v);
    });
}
}
