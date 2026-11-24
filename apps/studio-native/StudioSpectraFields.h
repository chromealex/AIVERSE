// Studio edit-world field publication: copied authored mesh heights and
// NEREID scene sources, not a second live gameplay simulation.
#pragma once
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "engine/fx/SpectraDataInterfaces.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/nereid/NereidWaterSystems.h"
#include <memory>
#include <vector>
namespace aiverse::studio {
class StudioSpectraTerrainSnapshot {
    int width_=0,height_=0,step_=1,columns_=0,rows_=0;
    std::vector<float> values_;
public:
    static std::shared_ptr<const StudioSpectraTerrainSnapshot> capture(
        const editor::MapData& map,float levelHeight) {
        if(map.planet() || map.worldSpace ||
           map.extra.getStr("terrainPresentation","legacy")=="entities" ||
           map.w<=0||map.h<=0||map.w>200000||map.h>200000||
           !std::isfinite(levelHeight))return {};
        auto out=std::make_shared<StudioSpectraTerrainSnapshot>();
        out->width_=map.w;out->height_=map.h;
        out->step_=std::max(1,(std::max(map.w,map.h)+255)/256);
        out->columns_=(map.w+out->step_-1)/out->step_+1;
        out->rows_=(map.h+out->step_-1)/out->step_+1;
        out->values_.reserve(size_t(out->columns_)*out->rows_);
        for(int z=0;z<out->rows_;++z)
            for(int x=0;x<out->columns_;++x) {
                const float value=terrainPreviewCorner(map,std::min(x*out->step_,map.w),
                    std::min(z*out->step_,map.h),levelHeight);
                if(!std::isfinite(value))return {};
                out->values_.push_back(value);
            }
        return out;
    }
    bool sample(std::string_view channel,render::float3 p,float& value)const {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
           p.x<0||p.z<0||p.x>width_||p.z>height_)return false;
        const int x=std::min(int(p.x/step_),columns_-2);
        const int z=std::min(int(p.z/step_),rows_-2);
        const int x0=x*step_,z0=z*step_;
        const float u=(p.x-x0)/float(std::min(x0+step_,width_)-x0);
        const float v=(p.z-z0)/float(std::min(z0+step_,height_)-z0);
        const auto at=[&](int i,int j){return values_[size_t(j)*columns_+i];};
        const float a=at(x,z),b=at(x+1,z),c=at(x+1,z+1),d=at(x,z+1);
        // Same a-c diagonal as buildTerrainPreview; not bilinear.
        const float h=u>=v?a+(b-a)*u+(c-b)*v:a+(d-a)*v+(c-d)*u;
        float result;
        if(channel=="height")result=h;
        else if(channel=="clearance")result=p.y-h;
        else if(channel=="up_x"||channel=="up_z")result=0;
        else if(channel=="up_y")result=1;
        else return false;
        if(!std::isfinite(result)||std::abs(result)>1e7f)return false;
        value=result;return true;
    }
};

inline std::shared_ptr<const spectra::DataInterfaceRegistry> buildStudioSpectraFields(
        const editor::MapData& map,float levelHeight,bool showTerrain,
        const world::NereidWaterScene* water) {
    auto registry=std::make_shared<spectra::DataInterfaceRegistry>();
    if(map.planet()||map.worldSpace)return registry;
    if(showTerrain)if(auto terrain=StudioSpectraTerrainSnapshot::capture(map,levelHeight))
        registry->bind("terraweave",[terrain](std::string_view c,render::float3 p,float& v){
            return terrain->sample(c,p,v);
        });
    if(water && water->surfaceCount()>0) {
        auto surfaces=std::make_shared<std::vector<world::NereidSceneSurface>>();
        water->forEachSource([&](const auto& s){surfaces->push_back(s);});
        // Match NEREID priority/surface ordering; stable authored id breaks ties.
        std::stable_sort(surfaces->begin(),surfaces->end(),[](const auto& a,const auto& b){
            if(!a.id)return false;
            if(!b.id)return true;
            return a.id<b.id;
        });
        registry->bind("nereid",[surfaces](std::string_view c,render::float3 p,float& v){
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
               std::abs(p.x)>1e6f||std::abs(p.y)>1e6f||std::abs(p.z)>1e6f)return false;
            const auto fixed=[](float x){return Fixed::fromRaw(int64_t(std::llround(double(x)*Fixed::kOne)));};
            const Vec3 at{fixed(p.x),fixed(p.y),fixed(p.z)};
            lib::nereid::WaterSample result;
            uint32_t rank=0;
            for(const auto& s:*surfaces) {
                const Entity candidate{rank++,1}; // tie-break only; not a live ECS entity
                const auto* geometry=s.useGeometry?&s.geometry:nullptr;
                if(!lib::nereid::coversWaterBody(s.body,geometry,at))continue;
                lib::nereid::considerWaterCandidate(result,candidate,s.body,at,geometry,
                    s.useCurrentField?&s.currentField:nullptr,s.useVortices?&s.vortices:nullptr,
                    s.mesh.useGrade?&s.mesh.grade:nullptr);
            }
            if(c=="found"){v=result.found?1.f:0.f;return true;}
            if(!result.found)return false;
            if(c=="submerged"){v=result.submerged?1.f:0.f;return true;}
            Fixed n{};
            if(c=="depth")n=result.depth;
            else if(c=="thickness")n=result.surfaceY-result.bottomY;
            else if(c=="surface_x")n=at.x;
            else if(c=="surface_y")n=result.surfaceY;
            else if(c=="surface_z")n=at.z;
            else if(c=="up_y")n=Fixed::fromInt(1);
            else if(c=="up_x"||c=="up_z")n=Fixed{};
            else if(c=="flow_x")n=result.flow.x;
            else if(c=="flow_y")n=result.flow.y;
            else if(c=="flow_z")n=result.flow.z;
            else return false;
            const double value=n.toDouble();
            if(!std::isfinite(value)||std::abs(value)>1e7)return false;
            v=float(value);return true;
        });
    }
    return registry;
}
}
