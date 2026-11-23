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
#include <set>
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
// Stable-ID neighbor edge snap, in map-local centimetres of the active
// owner. Compare all authored EVOLVE rectangles without touching live ECS.
// At equal distances choose lexicographically smaller stable map ID.
struct EdgeSnap {
    bool found=false;
    float valueCm=0;
    std::string targetId;
    float guideStart=0,guideEnd=0; // overlap along perpendicular world axis, metres
};
inline EdgeSnap snapNeighborEdge(const editor::MapDoc& map,
                                const editor::DocEntity& owner,
                                Handle::Mode mode,float proposedCm,
                                float thresholdCm=100.f){
    const bool x=mode==Handle::Mode::AreaWidth||
        mode==Handle::Mode::AreaWidthNegative;
    const bool z=mode==Handle::Mode::AreaDepth||
        mode==Handle::Mode::AreaDepthNegative;
    if((!x&&!z)||!std::isfinite(proposedCm)||thresholdCm<0)return {};
    EdgeSnap best;
    double bestDistance=double(thresholdCm)+1.;
    for(const auto& other:map.data.entities){
        if(other.id==owner.id||other.id.empty())continue;
        const auto& components=other.extra["components"];
        if(!components["EvolveRegion"].isObject())continue;
        const auto& area=components["EvolveArea"];
        if(!area.isObject()||!area["enabled"].boolean())continue;
        // Only physically neighboring edge segments can attract a drag.
        // Their perpendicular spans must overlap with positive length.
        const auto& ownArea=owner.extra["components"]["EvolveArea"];
        if(!ownArea.isObject())continue;
        const auto ownHalf=ownArea.getInt(x?"halfDepthCm":"halfWidthCm",0);
        const auto otherHalf=area.getInt(x?"halfDepthCm":"halfWidthCm",0);
        if(ownHalf<50||otherHalf<50||
           ownHalf>evolve::kAreaMaxHalfExtentCm||
           otherHalf>evolve::kAreaMaxHalfExtentCm)continue;
        const double ownCross=(x?double(owner.z):double(owner.x))*100.+
            double(ownArea.getInt(x?"offsetZcm":"offsetXcm",0));
        const double otherCross=(x?double(other.z):double(other.x))*100.+
            double(area.getInt(x?"offsetZcm":"offsetXcm",0));
        const double overlapStart=std::max(ownCross-double(ownHalf),
                                           otherCross-double(otherHalf));
        const double overlapEnd=std::min(ownCross+double(ownHalf),
                                         otherCross+double(otherHalf));
        if(overlapEnd<=overlapStart)continue;
        const auto half=area.getInt(x?"halfWidthCm":"halfDepthCm",0);
        if(half<50||half>evolve::kAreaMaxHalfExtentCm)continue;
        const double origin=x?double(owner.x):double(owner.z);
        const double center=(x?double(other.x):double(other.z))*100.+
            double(area.getInt(x?"offsetXcm":"offsetZcm",0));
        for(const int sign:{-1,1}){
            const double edge=center+double(sign)*double(half)-origin*100.;
            const double distance=std::abs(edge-double(proposedCm));
            if(distance>double(thresholdCm))continue;
            if(distance<bestDistance||
               (distance==bestDistance&&(!best.found||other.id<best.targetId))){
                bestDistance=distance;
                best={true,float(edge),other.id,
                      float(overlapStart/100.),float(overlapEnd/100.)};
            }
        }
    }
    return best;
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
// Mirrors the Stage40 group resize ratio and pivot, without mutating MapDoc.
// Invalid members reject the entire preview rather than drawing partial data.
inline bool previewGroupResize(const editor::MapDoc& map,
                               const std::vector<std::string>& selected,
                               const std::string& ownerId,Handle::Mode mode,
                               double proposedCm,bool individual,
                               std::vector<AreaPreview>& output,
                               render::float3& pivotPoint){
    output.clear();
    const bool x=mode==Handle::Mode::AreaWidth||
                 mode==Handle::Mode::AreaWidthNegative;
    const bool z=mode==Handle::Mode::AreaDepth||
                 mode==Handle::Mode::AreaDepthNegative;
    if((!x&&!z)||selected.size()<2||selected.size()>256)return false;
    const int activeIndex=map.findEntity(ownerId);
    if(activeIndex<0)return false;
    const auto& active=map.data.entities[std::size_t(activeIndex)];
    const auto& activeArea=active.extra["components"]["EvolveArea"];
    if(!activeArea.isObject())return false;
    AreaPreview activePreview;
    if(!previewArea(active,mode,proposedCm,individual,activePreview))
        return false;
    const double oldHalf=double(activeArea.getInt(x?"halfWidthCm":"halfDepthCm",0));
    const double newHalf=(x?activePreview.halfWidth:activePreview.halfDepth)*100.;
    if(oldHalf<50||newHalf<50)return false;
    const auto n=std::llround(newHalf),d=std::llround(oldHalf);
    if(n<1||n>1000000||d<1||d>1000000)return false;
    std::vector<const editor::DocEntity*> entities;
    std::vector<std::int64_t> centers;
    std::int64_t sum=0;
    for(const auto& id:selected){
        const int index=map.findEntity(id.rfind("entity:@",0)==0?id.substr(8):id);
        if(index<0)return false;
        const auto& e=map.data.entities[std::size_t(index)];
        const auto& area=e.extra["components"]["EvolveArea"];
        if(!area.isObject()||!e.extra["components"]["EvolveRegion"].isObject())
            return false;
        const auto center=std::llround(double(x?e.x:e.z)*100.)+
            area.getInt(x?"offsetXcm":"offsetZcm",0);
        entities.push_back(&e);centers.push_back(center);sum+=center;
    }
    const auto shared=std::llround(double(sum)/double(centers.size()));
    pivotPoint=x?render::float3{float(shared)/100.f,.65f,active.z}:
                 render::float3{active.x,.65f,float(shared)/100.f};
    for(std::size_t i=0;i<entities.size();++i){
        const auto& e=*entities[i];
        const auto& area=e.extra["components"]["EvolveArea"];
        const auto half=area.getInt(x?"halfWidthCm":"halfDepthCm",0);
        const auto scaled=std::llround((long double)half*n/d);
        if(scaled<50||scaled>evolve::kAreaMaxHalfExtentCm)return false;
        const auto center=individual?centers[i]:
            shared+std::llround((long double)(centers[i]-shared)*n/d);
        AreaPreview preview;
        preview.centerX=double(e.x)+double(area.getInt("offsetXcm",0))/100.;
        preview.centerZ=double(e.z)+double(area.getInt("offsetZcm",0))/100.;
        preview.halfWidth=double(area.getInt("halfWidthCm",0))/100.;
        preview.halfDepth=double(area.getInt("halfDepthCm",0))/100.;
        if(x){preview.centerX=double(center)/100.;preview.halfWidth=double(scaled)/100.;}
        else {preview.centerZ=double(center)/100.;preview.halfDepth=double(scaled)/100.;}
        if(preview.centerX*100.-preview.halfWidth*100.<-double(evolve::kAreaCoordinateCm)||
           preview.centerX*100.+preview.halfWidth*100.>double(evolve::kAreaCoordinateCm)||
           preview.centerZ*100.-preview.halfDepth*100.<-double(evolve::kAreaCoordinateCm)||
           preview.centerZ*100.+preview.halfDepth*100.>double(evolve::kAreaCoordinateCm))
            return false;
        output.push_back(preview);
    }
    return true;
}
// Stage43 numeric two-axis preview. The same rational inputs and custom
// world-space pivot are accepted by studio.worldProcessAreaBatchResize.
inline bool previewGroupScale2D(
    const editor::MapDoc& map,const std::vector<std::string>& selected,
    std::int64_t numeratorX,std::int64_t denominatorX,
    std::int64_t numeratorZ,std::int64_t denominatorZ,
    std::int64_t pivotXcm,std::int64_t pivotZcm,
    std::vector<AreaPreview>& output){
    output.clear();
    if(selected.size()<2||selected.size()>256||
       numeratorX<1||denominatorX<1||numeratorZ<1||denominatorZ<1||
       numeratorX>1000000||denominatorX>1000000||
       numeratorZ>1000000||denominatorZ>1000000)return false;
    std::set<std::string> unique;
    for(const auto& stable:selected){
        if(!unique.insert(stable).second)return false;
        const std::string id=stable.rfind("entity:@",0)==0?
            stable.substr(8):stable;
        const int index=map.findEntity(id);
        if(index<0)return false;
        const auto& entity=map.data.entities[std::size_t(index)];
        const auto& components=entity.extra["components"];
        const auto& area=components["EvolveArea"];
        if(!components["EvolveRegion"].isObject()||
           !area.isObject()||!area["enabled"].boolean())return false;
        const auto cx=std::llround(double(entity.x)*100.)+
            area.getInt("offsetXcm",0);
        const auto cz=std::llround(double(entity.z)*100.)+
            area.getInt("offsetZcm",0);
        const auto hx=std::llround((long double)area.getInt("halfWidthCm",0)*
                                    numeratorX/denominatorX);
        const auto hz=std::llround((long double)area.getInt("halfDepthCm",0)*
                                    numeratorZ/denominatorZ);
        const auto nx=pivotXcm+std::llround((long double)(cx-pivotXcm)*
                                            numeratorX/denominatorX);
        const auto nz=pivotZcm+std::llround((long double)(cz-pivotZcm)*
                                            numeratorZ/denominatorZ);
        if(hx<50||hx>evolve::kAreaMaxHalfExtentCm||
           hz<50||hz>evolve::kAreaMaxHalfExtentCm||
           nx-hx < -evolve::kAreaCoordinateCm||
           nx+hx > evolve::kAreaCoordinateCm||
           nz-hz < -evolve::kAreaCoordinateCm||
           nz+hz > evolve::kAreaCoordinateCm)return false;
        output.push_back({double(nx)/100.,double(nz)/100.,
                          double(hx)/100.,double(hz)/100.});
    }
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
