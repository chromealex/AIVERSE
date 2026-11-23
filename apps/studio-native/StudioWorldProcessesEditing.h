// Interactive World Processes Scene handles. Geometry and hit testing
// are read-only; the actual edit is submitted to StudioSession on mouse-up.
// No runtime ECS mutation or independent editor Undo stack.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "apps/studio-native/StudioScene.h"
#include "libraries/include/aiverselib/world/WorldProcessesExtendedLinks.h"
#include "libraries/include/aiverselib/world/WorldProcessesLinkedAuthoring.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::studio::worldprocess_edit {

struct Handle {
    enum class Mode:std::uint8_t {Reference,Radius,GridWidth,GridHeight,AreaWidth,AreaDepth,AreaWidthNegative,AreaDepthNegative,AreaCenterX,AreaCenterZ};
    Mode mode=Mode::Reference;
    std::string owner,component,key,targetType;
    render::float3 position{};
    float baseline=0; // editor-unit radius or grid size in metres
};
inline render::float3 point(const editor::DocEntity& entity,float y=.35f){
    return {entity.x,y,entity.z};
}
inline std::vector<Handle> handles(const editor::MapDoc& map,
                                   std::string_view selected) {
    std::vector<Handle> out;
    std::string id(selected);
    if(id.rfind("entity:@",0)==0)id.erase(0,8);
    if(id.empty())return out;
    const int ownerIndex=map.findEntity(id);
    if(ownerIndex<0)return out;
    const auto& owner=map.data.entities[std::size_t(ownerIndex)];
    const auto& components=owner.extra["components"];
    if(!components.isObject())return out;
    for(const auto& [component,document]:components.members()){
        if(!document.isObject())continue;
        for(const auto& [key,value]:document.members()){
            std::string_view required;
            if(lib::worldprocess::extendedLinks::supports(component))
                required=lib::worldprocess::extendedLinks::referenceTarget(component,key);
            else if(lib::worldprocess::linked::isReference(component,key))
                required=lib::worldprocess::linked::targetType(component);
            if(required.empty()||!value.isString())continue;
            // Unbound refs have no endpoint to grab: add a small offset
            // "socket" next to their owning object, one per field.
            render::float3 where=point(owner);
            if(!value.str().empty()){
                const int at=map.findEntity(value.str());
                if(at>=0)where=point(map.data.entities[std::size_t(at)]);
            } else {
                const float offset=0.5f+float(out.size()%6)*.3f;
                where.x+=offset;
                where.z+=.5f;
            }
            out.push_back({Handle::Mode::Reference,owner.id,component,key,
                           std::string(required),where,0});
            if(out.size()>=48)return out;
        }
    }
    // Native EVOLVE ecology now reads this exact ECS-persisted Area.
    if(const auto& area=components["EvolveArea"];area.isObject()){
        const auto offsetX=area.getInt("offsetXcm",0);
        const auto offsetZ=area.getInt("offsetZcm",0);
        const float centerX=owner.x+float(offsetX)/100.f;
        const float centerZ=owner.z+float(offsetZ)/100.f;
        const auto width=area.getInt("halfWidthCm",1000);
        const auto depth=area.getInt("halfDepthCm",1000);
        if(width>=50&&width<=evolve::kAreaMaxHalfExtentCm&&
           depth>=50&&depth<=evolve::kAreaMaxHalfExtentCm){
            out.push_back({Handle::Mode::AreaWidth,owner.id,"EvolveArea","halfWidthCm",
                           {},{centerX+float(width)/100.f,.35f,centerZ},0});
            out.push_back({Handle::Mode::AreaDepth,owner.id,"EvolveArea","halfDepthCm",
                           {},{centerX,.35f,centerZ+float(depth)/100.f},0});
            // Each opposing edge is draggable independently. The area stays
            // centered on its map entity; either edge edits the same persisted
            // half-extent and creates one MapDoc Undo entry on mouse release.
            out.push_back({Handle::Mode::AreaWidthNegative,owner.id,"EvolveArea","halfWidthCm",
                           {},{centerX-float(width)/100.f,.35f,centerZ},0});
            out.push_back({Handle::Mode::AreaDepthNegative,owner.id,"EvolveArea","halfDepthCm",
                           {},{centerX,.35f,centerZ-float(depth)/100.f},0});
            out.push_back({Handle::Mode::AreaCenterX,owner.id,"EvolveArea","offsetXcm",
                           {},{centerX,.35f,centerZ},0});
            out.push_back({Handle::Mode::AreaCenterZ,owner.id,"EvolveArea","offsetZcm",
                           {},{centerX,.55f,centerZ},0});
        }
    }else if(components["EvolveRegion"].isObject()){
        // Legacy visual-only rings remain editable for older maps until
        // the author explicitly adds EvolveArea to become authoritative.
        const float r=float(owner.extra["worldProcessGizmo"]
                                ["radiusMetres"].toDouble(10));
        if(std::isfinite(r))
            out.push_back({Handle::Mode::Radius,owner.id,"EvolveRegion","radiusMetres",
                           {},{owner.x+std::clamp(r,.5f,500.f),.35f,owner.z},r});
    }
    if(const auto& grid=components["GranularField"];grid.isObject()){
        const float cell=float(owner.extra["worldProcessGizmo"]
                               ["cellSizeMetres"].toDouble(1));
        const float size=std::isfinite(cell)?std::clamp(cell,.05f,32.f):1.f;
        const float width=float(grid.getInt("width",1));
        const float height=float(grid.getInt("height",1));
        if(width>=1&&width<=128&&height>=1&&height<=128){
            out.push_back({Handle::Mode::GridWidth,owner.id,"GranularField","width",
                          {},{owner.x+width*size/2.f,.35f,owner.z},size});
            out.push_back({Handle::Mode::GridHeight,owner.id,"GranularField","height",
                          {},{owner.x,.35f,owner.z+height*size/2.f},size});
        }
    }
    return out;
}
inline bool compatible(const editor::DocEntity& target,
                       std::string_view required){
    if(required=="Entity")return !target.id.empty();
    return !target.id.empty()&&
           target.extra["components"].has(std::string(required));
}
inline const editor::DocEntity* nearestTarget(
        const editor::MapDoc& map,const Handle& handle,
        const StudioScene& scene,double x,double y,double width,double height,
        float maxDistancePx=14.f) {
    const editor::DocEntity* selected=nullptr;
    double best=double(maxDistancePx*maxDistancePx);
    for(const auto& entity:map.data.entities){
        if(!compatible(entity,handle.targetType))continue;
        render::float2 screen{};
        if(!scene.projectPoint(point(entity),float(width),float(height),screen))continue;
        const double dx=double(screen.x)-x,dz=double(screen.y)-y;
        const double d=dx*dx+dz*dz;
        if(d<best){best=d;selected=&entity;}
    }
    return selected;
}
// All nearby handles are returned in stable distance/identity order. The
// native host can cycle overlapping sockets by repeated clicks without
// ambiguity or relying on ECS slot order.
inline std::vector<int> hitCandidates(const std::vector<Handle>& choices,
               const StudioScene& scene,
               double x,double y,double width,double height,
               float maxDistancePx=11.f){
    std::vector<std::pair<double,int>> near;
    const double threshold=double(maxDistancePx)*maxDistancePx;
    for(std::size_t i=0;i<choices.size();++i){
        render::float2 screen{};
        if(!scene.projectPoint(choices[i].position,float(width),float(height),screen))
            continue;
        const double dx=screen.x-x,dy=screen.y-y;
        const double distance=dx*dx+dy*dy;
        if(distance<=threshold)near.push_back({distance,int(i)});
    }
    std::sort(near.begin(),near.end(),[](const auto& lhs,const auto& rhs){
        if(lhs.first!=rhs.first)return lhs.first<rhs.first;
        return lhs.second<rhs.second;
    });
    std::vector<int> out;
    out.reserve(near.size());
    for(const auto& [distance,index]:near){
        (void)distance;
        out.push_back(index);
    }
    return out;
}
inline int hit(const std::vector<Handle>& choices,
               const StudioScene& scene,
               double x,double y,double width,double height,
               float maxDistancePx=11.f){
    auto near=hitCandidates(choices,scene,x,y,width,height,maxDistancePx);
    return near.empty()?-1:near.front();
}
// Preview-only geometry, derived from the same opposite-fixed edge arithmetic
// as studio.worldProcessAreaSet. Does not mutate MapDoc or ECS.
struct AreaPreview {
    double centerX=0,centerZ=0,halfWidth=0,halfDepth=0;
};
inline bool previewArea(const editor::DocEntity& owner,Handle::Mode mode,
                        double valueCm,bool symmetric,AreaPreview& result){
    const auto& area=owner.extra["components"]["EvolveArea"];
    if(!area.isObject()||!std::isfinite(valueCm))return false;
    const double ox=double(area.getInt("offsetXcm",0));
    const double oz=double(area.getInt("offsetZcm",0));
    const double hx=double(area.getInt("halfWidthCm",1000));
    const double hz=double(area.getInt("halfDepthCm",1000));
    double cx=ox,cz=oz,wx=hx,wz=hz;
    if(mode==Handle::Mode::AreaCenterX)cx=valueCm;
    else if(mode==Handle::Mode::AreaCenterZ)cz=valueCm;
    else{
        const bool x=mode==Handle::Mode::AreaWidth||
            mode==Handle::Mode::AreaWidthNegative;
        const bool z=mode==Handle::Mode::AreaDepth||
            mode==Handle::Mode::AreaDepthNegative;
        if(!x&&!z)return false;
        const bool positive=mode==Handle::Mode::AreaWidth||
            mode==Handle::Mode::AreaDepth;
        const double center=x?cx:cz,half=x?wx:wz;
        if(symmetric){
            const double extent=std::clamp(std::abs(valueCm-center),50.,50000.);
            if(x)wx=extent;else wz=extent;
        }else{
            const double opposite=center+(positive?-half:half);
            double edge=positive?
                std::clamp(valueCm,opposite+100.,opposite+100000.):
                std::clamp(valueCm,opposite-100000.,opposite-100.);
            // Match the integer-centimetre even-span rounding of StudioSession.
            edge=std::round(edge);
            if(std::fmod(std::abs(edge-opposite),2.)!=0.)
                edge+=positive?-1.:1.;
            if(x){cx=(edge+opposite)/2.;wx=std::abs(edge-opposite)/2.;}
            else {cz=(edge+opposite)/2.;wz=std::abs(edge-opposite)/2.;}
        }
    }
    const double worldX=double(owner.x)*100.+cx;
    const double worldZ=double(owner.z)*100.+cz;
    if(worldX-wx < -double(evolve::kAreaCoordinateCm)||
       worldX+wx > double(evolve::kAreaCoordinateCm)||
       worldZ-wz < -double(evolve::kAreaCoordinateCm)||
       worldZ+wz > double(evolve::kAreaCoordinateCm))return false;
    result={worldX/100.,worldZ/100.,wx/100.,wz/100.};
    return true;
}
inline bool extent(const editor::DocEntity& owner,const Handle& handle,
                   render::float3 mouse,float& value) {
    if(!std::isfinite(mouse.x)||!std::isfinite(mouse.z))return false;
    if(handle.mode==Handle::Mode::Radius) {
        const auto dx=double(mouse.x-owner.x),dz=double(mouse.z-owner.z);
        const double radius=std::hypot(dx,dz);
        if(!std::isfinite(radius))return false;
        value=std::clamp(float(radius),.5f,500.f);return true;
    }
    if(handle.mode==Handle::Mode::AreaCenterX||
       handle.mode==Handle::Mode::AreaCenterZ){
        const double relative=handle.mode==Handle::Mode::AreaCenterX?
            double(mouse.x-owner.x):double(mouse.z-owner.z);
        value=float(std::clamp(std::round(relative*100.),
            -double(evolve::kAreaCoordinateCm),double(evolve::kAreaCoordinateCm)));
        return true;
    }
    const auto& area=owner.extra["components"]["EvolveArea"];
    const double cx=double(owner.x)+double(area.getInt("offsetXcm",0))/100.;
    const double cz=double(owner.z)+double(area.getInt("offsetZcm",0))/100.;
    // The pointer gives the dragged edge in centimetres relative to map pos.
    // StudioSession atomically derives both half-extent and center offset,
    // holding the opposite edge fixed (Stage33 asymmetric resize).
    if(handle.mode==Handle::Mode::AreaWidth||
       handle.mode==Handle::Mode::AreaWidthNegative){
        value=float(std::clamp(std::round((double(mouse.x)-owner.x)*100.),
            -double(evolve::kAreaCoordinateCm),double(evolve::kAreaCoordinateCm)));
        return true;
    }
    if(handle.mode==Handle::Mode::AreaDepth||
       handle.mode==Handle::Mode::AreaDepthNegative){
        value=float(std::clamp(std::round((double(mouse.z)-owner.z)*100.),
            -double(evolve::kAreaCoordinateCm),double(evolve::kAreaCoordinateCm)));
        return true;
    }
    if(handle.mode==Handle::Mode::GridWidth){
        value=std::clamp(float(std::lround(
            std::abs(double(mouse.x-owner.x))*2/handle.baseline)),1.f,128.f);
        return true;
    }
    if(handle.mode==Handle::Mode::GridHeight){
        value=std::clamp(float(std::lround(
            std::abs(double(mouse.z-owner.z))*2/handle.baseline)),1.f,128.f);
        return true;
    }
    return false;
}
} // namespace aiverse::studio::worldprocess_edit
