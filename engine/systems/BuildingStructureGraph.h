#pragma once
// CPU-only functional layout graph: exact openings, structural wall segments,
// movable panels, and semantic sockets. No style/material, genre, editor, ECS
// or renderer dependency. Distances are metres. Side coordinates run +X on
// front/back and +Z on left/right (building-local axes).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::lib::systems {

enum class BuildingFeatureKind : uint8_t {
    LargeGate, SmallGate, PersonnelDoor, SlidingRoof, Window, Stairs, Elevator
};
enum class BuildingFeatureSide : uint8_t { Front, Back, Left, Right };
enum class BuildingBalconySupport : uint8_t { Cantilever, Columns };
struct BuildingFeature {
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t count=1;
    float width=1.2f,height=2.4f,depth=0.f;
    float open=0.f;
    // Negative means automatic placement. For wall features, position is the
    // group centre along that wall; count>1 repeats with 'spacing' clear gap.
    // For roof: position = X centre, positionZ = Z centre.
    float position=-1.f,positionZ=-1.f,spacing=0.35f;
    uint32_t floor=0;
    float sill=0.9f;
    // -1 = original outer envelope facade. Otherwise a room-grid line.
    int wallLine=-1;
    // Optional deck projecting from an upper personnel door into an empty grid cell.
    float balconyDepth=0.f;
    BuildingBalconySupport balconySupport=BuildingBalconySupport::Cantilever;
};
enum class FunctionalRoofProfile : uint8_t { Flat, Gable, Shed, Barrel, Hip, Dome, Terrace, Pagoda };
struct FunctionalBuildingRecipe {
    float width=16.f,height=8.f,depth=12.f;
    uint64_t seed=1;
    uint32_t floors=1,roomsX=1,roomsZ=1;
    std::vector<BuildingFeature> features;
    // Final height includes the optional cap. Existing recipes remain flat.
    FunctionalRoofProfile roofProfile=FunctionalRoofProfile::Flat;
    float roofRise=0.f;
    // Row-major occupancy, front to back; empty means legacy full rectangle.
    std::vector<std::string> footprint;
    // Optional explicit masks, one per storey. Upper cells require support below.
    std::vector<std::vector<std::string>> floorFootprints;
    bool walkableSetbacks=false;
    // Front-left, front-right, back-right, back-left cut distances in metres.
    std::array<float,4> cornerChamfers{};
};
inline bool functionalCell(const FunctionalBuildingRecipe& r,int x,int z,uint32_t floor=0){
    if(x<0||z<0||x>=int(r.roomsX)||z>=int(r.roomsZ)||floor>=r.floors)return false;
    const auto& mask=r.floorFootprints.empty()?r.footprint:r.floorFootprints[floor];
    return mask.empty()||mask[size_t(z)][size_t(x)]=='1';
}
inline bool functionalTerraceCell(const FunctionalBuildingRecipe& r,int x,int z,uint32_t floor){
    return r.walkableSetbacks&&floor>0&&floor<r.floors&&
        functionalCell(r,x,z,floor-1)&&!functionalCell(r,x,z,floor);
}
inline bool nonRectangularFootprint(const FunctionalBuildingRecipe& r){
    for(const auto& row:r.footprint)if(row.find('0')!=std::string::npos)return true;
    for(const auto& mask:r.floorFootprints)for(const auto& row:mask)if(row.find('0')!=std::string::npos)return true;
    return false;
}
inline const char* functionalRoofProfileName(FunctionalRoofProfile profile){
    switch(profile){
      case FunctionalRoofProfile::Flat:return "flat";
      case FunctionalRoofProfile::Gable:return "gable";
      case FunctionalRoofProfile::Shed:return "shed";
      case FunctionalRoofProfile::Barrel:return "barrel";
      case FunctionalRoofProfile::Hip:return "hip";
      case FunctionalRoofProfile::Dome:return "dome";
      case FunctionalRoofProfile::Terrace:return "terrace";
      case FunctionalRoofProfile::Pagoda:return "pagoda";
    }
    return "unknown";
}
inline float functionalCoreHeight(const FunctionalBuildingRecipe& r){return r.height-r.roofRise;}
inline float functionalRoofFraction(FunctionalRoofProfile profile,float x,float z){
    // x,z are normalized to [-1,1]; integer grid stations include the
    // summit. All profiles have a deterministic closed, supported cap.
    const float ax=std::abs(x),az=std::abs(z);
    switch(profile){
      case FunctionalRoofProfile::Gable:return std::max(0.f,1.f-ax);
      case FunctionalRoofProfile::Shed:return std::clamp((x+1.f)*.5f,0.f,1.f);
      case FunctionalRoofProfile::Barrel:return std::max(0.f,1.f-x*x);
      case FunctionalRoofProfile::Hip:return std::max(0.f,1.f-std::max(ax,az));
      case FunctionalRoofProfile::Dome:return std::max(0.f,1.f-x*x-z*z);
      case FunctionalRoofProfile::Terrace:return std::max(0.f,1.f-std::max(ax,az));
      case FunctionalRoofProfile::Pagoda:return std::max(0.f,1.f-std::max(ax,az)*std::max(ax,az));
      default:return 0.f;
    }
}
enum class BuildingGraphPart : uint8_t {
    Floor, Wall, Trim, DoorLeaf, Roof, RoofPanel, WindowPane, Partition,
    StairTread, StairLanding, ElevatorPlatform, ElevatorGuide
};
struct BuildingGraphSolid {
    BuildingGraphPart part=BuildingGraphPart::Wall;
    float x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;
    uint32_t featureIndex=UINT32_MAX;
    // Optional graph-authored corner heights in x0z0,x1z0,x1z1,x0z1 order.
    std::array<float,4> roofTop{-1.f,-1.f,-1.f,-1.f};
    bool collisionOnly=false;
};
struct BuildingGraphSocket {
    std::string id; // stable within one recipe: entrance/<feature>/<copy> or roof/<feature>
    BuildingFeatureKind kind=BuildingFeatureKind::PersonnelDoor;
    BuildingFeatureSide side=BuildingFeatureSide::Front;
    uint32_t featureIndex=0,repeatIndex=0;
    float x=0,y=0,z=0;
    float width=0,height=0,depth=0,open=0;
    // Actual local facade plane; negative retains legacy outer bounds.
    float wallPlane=-1.f;
    uint32_t floor=0;
    float openingBottom=.24f; // closed door lower edge in building-local metres
};
inline float buildingSocketWallPlane(const BuildingGraphSocket& s,float W,float D){
    if(s.wallPlane>=0.f)return s.wallPlane;
    return s.side==BuildingFeatureSide::Back?D:s.side==BuildingFeatureSide::Right?W:0.f;
}
struct BuildingGraphRoom {std::string id;uint32_t floor=0,x=0,z=0;float x0=0,y0=0,z0=0,x1=0,y1=0,z1=0;};
enum class BuildingConnectionKind : uint8_t { Passage, Stair, Elevator };
struct BuildingGraphConnection {std::string id,from,to;uint32_t floor=0;float x=0,y=0,z=0,width=1.2f,height=2.2f;
    BuildingConnectionKind kind=BuildingConnectionKind::Passage;
};
// Authored aperture overlap with a ground-floor room, not a clearance guarantee.
struct BuildingExteriorAperture {std::string socketId,roomId;float start=0,end=0;};
struct BuildingGraphTerrace {uint32_t floor=0,x=0,z=0;float x0=0,y=0,z0=0,x1=0,z1=0;};
struct BuildingGraphBalcony {std::string socketId;uint32_t floor=0,x=0,z=0;float x0=0,y=0,z0=0,x1=0,z1=0;BuildingBalconySupport support=BuildingBalconySupport::Cantilever;};
struct BuildingTerraceAccess {std::string socketId,roomId;uint32_t floor=0,x=0,z=0;};
struct BuildingRoofSection {float x0=0,z0=0,x1=0,z1=0,base=0,rise=0;bool ridgeAlongX=false;};
struct BuildingGraphVisualPrism {BuildingGraphPart part;float y0,y1;std::vector<std::array<float,2>> polygon;};
struct BuildingStructureGraph {
    std::vector<BuildingGraphSolid> solids;
    std::vector<BuildingGraphSocket> sockets;
    std::vector<BuildingGraphRoom> rooms;
    std::vector<BuildingGraphConnection> connections;
    std::string error;
    std::vector<BuildingExteriorAperture> exteriorApertures;
    std::vector<BuildingGraphTerrace> terraces;
    std::vector<BuildingTerraceAccess> terraceAccess;
    std::vector<BuildingGraphBalcony> balconies;
    std::vector<BuildingRoofSection> roofSections;
    std::vector<BuildingGraphVisualPrism> visualPrisms;
    std::vector<std::array<float,2>> footprintPolygon;
    explicit operator bool() const noexcept {return error.empty();}
};
inline const char* buildingFeatureName(BuildingFeatureKind kind) {
    switch(kind){
      case BuildingFeatureKind::LargeGate:return "large-gate";
      case BuildingFeatureKind::SmallGate:return "small-gate";
      case BuildingFeatureKind::PersonnelDoor:return "personnel-door";
      case BuildingFeatureKind::SlidingRoof:return "sliding-roof";
      case BuildingFeatureKind::Window:return "window";
      case BuildingFeatureKind::Stairs:return "stairs";
      case BuildingFeatureKind::Elevator:return "elevator";
    }
    return "unknown";
}
inline const char* buildingFeatureSideName(BuildingFeatureSide side) {
    switch(side){
      case BuildingFeatureSide::Front:return "front";
      case BuildingFeatureSide::Back:return "back";
      case BuildingFeatureSide::Left:return "left";
      case BuildingFeatureSide::Right:return "right";
    }
    return "unknown";
}
inline std::string validateFunctionalRecipe(const FunctionalBuildingRecipe& recipe) {
    const auto finite=[](float v){return std::isfinite(v);};
    if(!finite(recipe.width)||!finite(recipe.height)||!finite(recipe.depth)||
       recipe.width<8.f||recipe.width>160.f||recipe.depth<8.f||recipe.depth>160.f||
       recipe.height<4.f||recipe.height>80.f)
        return "Functional building W/H/D must fit 8..160 / 4..80 / 8..160 metres";
    if(!finite(recipe.roofRise)||uint8_t(recipe.roofProfile)>uint8_t(FunctionalRoofProfile::Pagoda)||
       (recipe.roofProfile==FunctionalRoofProfile::Flat?recipe.roofRise!=0.f:
          (recipe.roofRise<.5f||recipe.roofRise>std::min(12.f,recipe.height*.45f))))
        return "Invalid architectural roof profile or rise";
    if(recipe.features.size()>24)return "Too many building properties (maximum 24)";
    if(recipe.floors<1||recipe.floors>8||recipe.roomsX<1||recipe.roomsX>6||recipe.roomsZ<1||recipe.roomsZ>6||recipe.floors*recipe.roomsX*recipe.roomsZ>128)
        return "Invalid floors or room counts";
    if(!recipe.floorFootprints.empty()&&(!recipe.footprint.empty()||recipe.floorFootprints.size()!=recipe.floors))
        return "Use either one footprint or exactly one mask per floor";
    const auto validMask=[&](const std::vector<std::string>& mask){
        if(mask.size()!=recipe.roomsZ)return false;
        for(const auto& row:mask)if(row.size()!=recipe.roomsX||row.find_first_not_of("01")!=std::string::npos)return false;
        return true;
    };
    if(!recipe.footprint.empty()&&!validMask(recipe.footprint))return "Invalid footprint dimensions or cells";
    for(const auto& mask:recipe.floorFootprints)if(!validMask(mask))return "Invalid floor footprint dimensions or cells";
    if(!recipe.footprint.empty()||!recipe.floorFootprints.empty()){
      const uint32_t masks=recipe.floorFootprints.empty()?1:recipe.floors;
      for(uint32_t floor=0;floor<masks;++floor){
        std::vector<std::array<int,2>> queue;
        bool left=false,right=false,front=false,back=false;
        size_t count=0;
        for(int z=0;z<int(recipe.roomsZ);++z)for(int x=0;x<int(recipe.roomsX);++x)
            if(functionalCell(recipe,x,z,floor)){
                if(floor&&!functionalCell(recipe,x,z,floor-1))return "Upper footprint requires occupied support below";
                if(queue.empty())queue.push_back({x,z});
                ++count;left|=x==0;right|=x+1==int(recipe.roomsX);
                front|=z==0;back|=z+1==int(recipe.roomsZ);
            }
        if(queue.empty())return "Every storey needs an occupied cell";
        if(floor==0&&(!left||!right||!front||!back))return "Ground footprint must occupy all four envelope extrema";
        std::array<bool,36> seen{};seen[size_t(queue[0][1])*6+size_t(queue[0][0])]=true;
        for(size_t i=0;i<queue.size();++i){
            const auto c=queue[i];
            for(auto d:std::array<std::array<int,2>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
                const int x=c[0]+d[0],z=c[1]+d[1];
                if(functionalCell(recipe,x,z,floor)&&!seen[size_t(z)*6+size_t(x)]){
                    seen[size_t(z)*6+size_t(x)]=true;queue.push_back({x,z});
                }
            }
        }
        if(queue.size()!=count)return "Footprint cells must be edge-connected";
      }
    }
    const bool chamfered=std::any_of(recipe.cornerChamfers.begin(),recipe.cornerChamfers.end(),[](float c){return c!=0.f;});
    const float maxCut=std::min(4.f,.4f*std::min(recipe.width/recipe.roomsX,recipe.depth/recipe.roomsZ));
    for(float c:recipe.cornerChamfers)if(!finite(c)||c<0.f||c>maxCut||(c>0.f&&c<.5f))
        return "Corner cut must be zero or .5..4 metres and fit its corner room";
    if(chamfered&&(nonRectangularFootprint(recipe)||recipe.roofProfile!=FunctionalRoofProfile::Flat||recipe.walkableSetbacks))
        return "Chamfers currently require a rectangular room mask and flat roof";
    if(recipe.walkableSetbacks&&(recipe.floorFootprints.empty()||recipe.floors<2))
        return "Walkable setbacks require independent floors";
    if(recipe.walkableSetbacks){
        bool hasSetback=false;
        for(uint32_t f=1;f<recipe.floors;++f)for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x)
            hasSetback|=functionalTerraceCell(recipe,int(x),int(z),f);
        if(!hasSetback)return "Walkable setbacks require at least one supported removed upper cell";
    }
    const float storey=(functionalCoreHeight(recipe)-0.52f)/float(recipe.floors);
    if(storey<2.8f||recipe.width/float(recipe.roomsX)<2.5f||recipe.depth/float(recipe.roomsZ)<2.5f)
        return "Storey or room dimensions too small";
    uint32_t roofs=0,portals=0,verticals=0;
    for(const auto& f:recipe.features){
        if(!finite(f.width)||!finite(f.height)||!finite(f.depth)||!finite(f.open)||
           !finite(f.position)||!finite(f.positionZ)||!finite(f.spacing)||
           f.open<0||f.open>1||f.count<1||f.count>8||
           (f.position<0.f&&f.position!=-1.f)||
           (f.positionZ<0.f&&f.positionZ!=-1.f)||
           f.spacing<0.35f||f.spacing>8.f)
            return "Invalid feature count, placement, spacing or open fraction";
        const bool wallFeature=f.kind!=BuildingFeatureKind::SlidingRoof&&f.kind!=BuildingFeatureKind::Stairs&&f.kind!=BuildingFeatureKind::Elevator;
        const int lineCount=int((f.side==BuildingFeatureSide::Front||f.side==BuildingFeatureSide::Back)?recipe.roomsZ:recipe.roomsX);
        if(uint8_t(f.side)>uint8_t(BuildingFeatureSide::Right)||f.wallLine<-1||f.wallLine>lineCount||
           (!wallFeature&&f.wallLine!=-1))
            return "Invalid facade grid line";
        if(!finite(f.balconyDepth)||f.balconyDepth<0.f||uint8_t(f.balconySupport)>uint8_t(BuildingBalconySupport::Columns))
            return "Invalid balcony depth or support";
        if(f.balconyDepth>0.f&&
           (f.kind!=BuildingFeatureKind::PersonnelDoor||f.floor==0||f.count!=1||f.position<0.f||
            recipe.floorFootprints.empty()||f.wallLine<=0||f.wallLine>=lineCount||
            f.balconyDepth<1.4f||f.balconyDepth>(f.balconySupport==BuildingBalconySupport::Cantilever?1.8f:3.f)))
            return "Balcony requires one upper personnel door, explicit inner facade and position, independent floors and bounded depth";
        if(f.kind==BuildingFeatureKind::SlidingRoof){
            if(nonRectangularFootprint(recipe)||chamfered)return "Retractable roof requires an uncut rectangular footprint";
            if(recipe.roofProfile!=FunctionalRoofProfile::Flat)return "Retractable roofs require the flat architectural profile";
            if(f.floor!=0)return "Roof floor must be zero";
            if(++roofs>1||f.count!=1||f.width<1||f.depth<1||
               f.width>recipe.width*0.48f||f.depth>recipe.depth*0.75f)
                return "Sliding roof requires one aperture fitting inside the roof";
            if(f.position>=0&&(f.position-f.width*0.5f<0.6f||
                               f.position+f.width*0.5f>recipe.width-0.6f))
                return "Sliding roof X position crosses its structural frame";
            if(f.positionZ>=0&&(f.positionZ-f.depth*0.5f<0.6f||
                                f.positionZ+f.depth*0.5f>recipe.depth-0.6f))
                return "Sliding roof Z position crosses its structural frame";
            continue;
        }
        if(f.kind==BuildingFeatureKind::Stairs||f.kind==BuildingFeatureKind::Elevator){
            if(++verticals>2||recipe.floors<2||f.count!=1||f.floor!=0)
                return "Vertical cores require 2+ floors, start at floor 0 and have count=1 (max 2)";
            if(f.kind==BuildingFeatureKind::Stairs){
                if(f.width<2.4f||f.depth<5.6f)
                    return "Switchback stairs require at least 2.4m width and 5.6m depth";
                const float storey=(functionalCoreHeight(recipe)-0.52f)/float(recipe.floors);
                // Switchback: two flights, maximum 0.18m rise and minimum
                // 0.24m run per tread, plus a 0.9m turning landing.
                const float climb=storey+0.24f;
                const int perFlight=int(std::ceil(climb/0.18f+0.0001f)+1)/2;
                if(storey>8.5f||
                   f.depth<0.9f+float(perFlight)*0.24f)
                    return "Stair well too short for <=0.18m risers and >=0.24m treads";
            }else if(f.width<1.4f||f.depth<1.4f)
                return "Lift platform requires at least 1.4 by 1.4 metres";
            if(f.width>recipe.width/float(recipe.roomsX)-0.7f||
               f.depth>recipe.depth/float(recipe.roomsZ)-0.7f)
                return "Vertical core must fit completely inside one room cell";
            continue;
        }
        if(f.floor>=recipe.floors)return "Opening floor out of range";
        const bool window=f.kind==BuildingFeatureKind::Window;
        if(!window&&f.floor!=0&&((!recipe.walkableSetbacks&&f.balconyDepth<=0.f)||f.kind!=BuildingFeatureKind::PersonnelDoor||f.position<0.f))
            return "Upper entrances require a personnel door with explicit position on a walkable setback";
        if(!std::isfinite(f.sill)||f.sill<0.f||f.sill>4.f)return "Invalid sill";
        portals+=f.count;
        if(portals>32)return "Too many wall apertures";
        const float maxWidth=window?6.f:f.kind==BuildingFeatureKind::LargeGate?20.f:
                             f.kind==BuildingFeatureKind::SmallGate?8.f:3.f;
        if(f.width<0.85f||f.width>maxWidth||
           f.height<(window?0.4f:2.f)||f.height>(window?3.5f:storey-0.6f)||
           (window&&(f.sill<0.65f||f.sill+f.height>storey-0.4f)))
            return "Opening too large for selected storey";
        const float length=(f.side==BuildingFeatureSide::Front||
                            f.side==BuildingFeatureSide::Back)?recipe.width:recipe.depth;
        if(f.position>=0){
            const float span=float(f.count)*f.width+float(f.count-1)*f.spacing;
            if(f.position-span*0.5f<0.72f||
               f.position+span*0.5f>length-0.72f)
                return "Explicit wall opening group extends outside the usable wall";
        }
    }
    return {};
}
// Convex polygon clipping in local X/Z. Used for both exact visible prisms
// and bounded axis-aligned physical strips; no separate decorative facade.
namespace buildingChamferDetail {
using Point=std::array<float,2>;
using Polygon=std::vector<Point>;
inline Polygon clip(const Polygon& poly,float a,float b,float c){
    Polygon out;if(poly.empty())return out;
    auto distance=[&](Point p){return a*p[0]+b*p[1]-c;};
    auto prev=poly.back();float d0=distance(prev);
    for(auto cur:poly){
        const float d1=distance(cur);
        if((d0>=0)!=(d1>=0)){
            const float t=d0/(d0-d1);
            out.push_back({prev[0]+(cur[0]-prev[0])*t,prev[1]+(cur[1]-prev[1])*t});
        }
        if(d1>=0)out.push_back(cur);
        prev=cur;d0=d1;
    }
    return out;
}
inline Polygon rectangle(float x0,float z0,float x1,float z1){return {{x0,z0},{x1,z0},{x1,z1},{x0,z1}};}
inline float area(const Polygon& p){
    float sum=0;for(size_t i=0;i<p.size();++i){const auto& a=p[i];const auto& b=p[(i+1)%p.size()];sum+=a[0]*b[1]-a[1]*b[0];}
    return std::abs(sum)*.5f;
}
inline std::array<float,2> span(const Polygon& p,float x){
    float lo=1e9f,hi=-1e9f;
    for(size_t i=0;i<p.size();++i){
        const auto a=p[i],b=p[(i+1)%p.size()];
        if(std::abs(x-a[0])<.0001f){lo=std::min(lo,a[1]);hi=std::max(hi,a[1]);}
        if(std::abs(b[0]-a[0])<.000001f||x<std::min(a[0],b[0])-.0001f||x>std::max(a[0],b[0])+.0001f)continue;
        const float t=std::clamp((x-a[0])/(b[0]-a[0]),0.f,1.f),z=a[1]+(b[1]-a[1])*t;
        lo=std::min(lo,z);hi=std::max(hi,z);
    }
    return {lo,hi};
}
inline bool apply(BuildingStructureGraph& graph,const FunctionalBuildingRecipe& r){
    const float W=r.width,D=r.depth;
    const auto& cuts=r.cornerChamfers;
    const std::array<std::array<float,3>,4> planes{{{1,1,cuts[0]},{-1,1,cuts[1]-W},{-1,-1,cuts[2]-W-D},{1,-1,cuts[3]-D}}};
    auto trim=[&](Polygon p){for(size_t i=0;i<4;++i)if(cuts[i]>0)p=clip(p,planes[i][0],planes[i][1],planes[i][2]);return p;};
    graph.footprintPolygon=trim(rectangle(0,0,W,D));
    // Preserve all aperture motion envelopes, even when the initial pose is open.
    for(const auto& s:graph.sockets){
        if(s.kind==BuildingFeatureKind::Stairs||s.kind==BuildingFeatureKind::Elevator)continue;
        const bool alongX=s.side==BuildingFeatureSide::Front||s.side==BuildingFeatureSide::Back;
        const float center=alongX?s.x:s.z;
        const float startCut=s.side==BuildingFeatureSide::Front?cuts[0]:s.side==BuildingFeatureSide::Back?cuts[3]:
            s.side==BuildingFeatureSide::Left?cuts[0]:cuts[1];
        const float endCut=s.side==BuildingFeatureSide::Front?cuts[1]:s.side==BuildingFeatureSide::Back?cuts[2]:
            s.side==BuildingFeatureSide::Left?cuts[3]:cuts[2];
        const float reserve=s.kind==BuildingFeatureKind::Window?s.width*.5f+.15f:s.width+.15f;
        if((startCut>0&&center-reserve<startCut+.36f)||(endCut>0&&center+reserve>(alongX?W:D)-endCut-.36f)){
            graph.error="Opening or full door travel intersects a chamfered corner";return false;
        }
    }
    auto original=std::move(graph.solids);graph.solids.clear();
    auto append=[&](const BuildingGraphSolid& source,const Polygon& poly){
        if(poly.size()<3||area(poly)<.00001f)return;
        graph.visualPrisms.push_back({source.part,source.y0,source.y1,poly});
        std::vector<float> xs;for(auto p:poly)xs.push_back(p[0]);
        std::sort(xs.begin(),xs.end());xs.erase(std::unique(xs.begin(),xs.end(),[](float a,float b){return std::abs(a-b)<.00001f;}),xs.end());
        for(size_t i=1;i<xs.size();++i){
            const auto l=span(poly,xs[i-1]),h=span(poly,xs[i]);
            const bool slope=std::abs(l[0]-h[0])>.0001f||std::abs(l[1]-h[1])>.0001f;
            const int steps=slope?std::max(1,int(std::ceil((xs[i]-xs[i-1])/.08f))):1;
            for(int j=0;j<steps;++j){
                const float x0=xs[i-1]+(xs[i]-xs[i-1])*j/steps,x1=xs[i-1]+(xs[i]-xs[i-1])*(j+1)/steps;
                const auto a=span(poly,x0),b=span(poly,x1);
                // Walkable slabs are inscribed, never provide support beyond
                // the exact visual outline. Walls use conservative coverage.
                const bool floor=source.part==BuildingGraphPart::Floor;
                const float z0=floor?std::max(a[0],b[0]):std::min(a[0],b[0]);
                const float z1=floor?std::min(a[1],b[1]):std::max(a[1],b[1]);
                if(x1-x0<.001f||z1-z0<.001f)continue;
                auto s=source;s.x0=x0;s.x1=x1;s.z0=z0;s.z1=z1;s.collisionOnly=true;graph.solids.push_back(s);
            }
        }
    };
    for(const auto& s:original){
        auto p=trim(rectangle(s.x0,s.z0,s.x1,s.z1));
        if(std::abs(area(p)-(s.x1-s.x0)*(s.z1-s.z0))<.0001f){graph.solids.push_back(s);continue;}
        if(s.featureIndex!=UINT32_MAX||s.part==BuildingGraphPart::Partition){
            graph.error="Chamfer intersects a feature or internal partition";return false;
        }
        append(s,p);
    }
    // A true diagonal wall band follows each cut plane, extending inward
    // by the perpendicular wall thickness and joining the straight walls.
    for(size_t i=0;i<4;++i)if(cuts[i]>0){
        const auto p=planes[i];
        const auto band=clip(graph.footprintPolygon,-p[0],-p[1],-(p[2]+.36f*1.41421356f));
        BuildingGraphSolid wall;wall.part=BuildingGraphPart::Wall;wall.y0=.24f;wall.y1=r.height;
        append(wall,band);
    }
    if(graph.solids.size()>20000){graph.error="Chamfer physical strip budget exceeded";return false;}
    return true;
}
} // namespace buildingChamferDetail

// Every wall and roof SOLID, door leaf and socket is compiled from one graph.
// Render/physics simply consume the same solids: openings cannot retain an
// unseen closed collider. Sockets remain stable under palette changes.
inline BuildingStructureGraph buildFunctionalStructureGraph(
    const FunctionalBuildingRecipe& recipe) {
    BuildingStructureGraph graph;
    graph.error=validateFunctionalRecipe(recipe);
    if(!graph.error.empty())return graph;
    const float W=recipe.width,H=functionalCoreHeight(recipe),D=recipe.depth;
    constexpr float floorTop=0.24f,wall=0.36f,roofThickness=0.28f,slabThick=0.24f;
    const float storey=(H-floorTop-roofThickness)/float(recipe.floors);
    auto solid=[&](BuildingGraphPart p,float x0,float y0,float z0,
                   float x1,float y1,float z1,uint32_t owner=UINT32_MAX){
        if(x1-x0>0.0001f&&y1-y0>0.0001f&&z1-z0>0.0001f)
            graph.solids.push_back({p,x0,y0,z0,x1,y1,z1,owner});
    };
    const bool shaped=nonRectangularFootprint(recipe);
    const float roomW=W/float(recipe.roomsX),roomD=D/float(recipe.roomsZ);
    const bool perFloor=!recipe.floorFootprints.empty();
    const auto occupiedCell=[&](int x,int z,uint32_t floor=0){return functionalCell(recipe,x,z,floor);};
    const auto coreCell=[&](int x,int z){return occupiedCell(x,z,recipe.floors-1);};
    auto occupiedPoint=[&](float x,float z,uint32_t floor){
        return occupiedCell(int(x/roomW),int(z/roomD),floor);
    };
    auto footprintSlab=[&](BuildingGraphPart part,float y0,float y1,uint32_t floor){
        if(!shaped){solid(part,0,y0,0,W,y1,D);return;}
        for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x)
            if(occupiedCell(int(x),int(z),floor))
                solid(part,x*roomW,y0,z*roomD,(x+1)*roomW,y1,(z+1)*roomD);
    };
    footprintSlab(BuildingGraphPart::Floor,0,floorTop,0);
    // Inter-storey slabs are cut AFTER core placement, so neither their
    // visible mesh nor the conservative collider fills a staircase/lift shaft.
    struct Hole {
        uint32_t feature=0,copy=0;
        BuildingFeature value;
        float start=0,end=0;
        bool placed=false;
    };
    std::array<std::array<std::array<std::vector<Hole>,7>,4>,8> walls;
    const BuildingFeature* roof=nullptr;
    uint32_t roofIndex=0;
    struct VerticalCore {BuildingFeatureKind kind;uint32_t feature;
        float x0,z0,x1,z1;uint32_t roomX,roomZ;};
    std::vector<VerticalCore> cores;
    for(uint32_t i=0;i<recipe.features.size();++i){
        const auto& f=recipe.features[i];
        if(f.kind==BuildingFeatureKind::SlidingRoof){roof=&f;roofIndex=i;continue;}
        if(f.kind==BuildingFeatureKind::Stairs||f.kind==BuildingFeatureKind::Elevator){
            // Unspecified cores use the first available room cell, not the
            // building centre where a partition intersection may be located.
            const uint32_t defaultX=cores.empty()?0:std::min(1u,recipe.roomsX-1);
            float cx=f.position>=0?f.position:(float(defaultX)+0.5f)*roomW;
            float cz=f.positionZ>=0?f.positionZ:roomD*0.5f;
            if(shaped&&(f.position<0||f.positionZ<0)){
                bool found=false;
                for(uint32_t z=0;z<recipe.roomsZ&&!found;++z)for(uint32_t x=0;x<recipe.roomsX&&!found;++x){
                    if(!coreCell(int(x),int(z)))continue;
                    const float tx=f.position>=0?f.position:(float(x)+.5f)*roomW;
                    const float tz=f.positionZ>=0?f.positionZ:(float(z)+.5f)*roomD;
                    if(tx-f.width*.5f<x*roomW+.35f||tx+f.width*.5f>(x+1)*roomW-.35f||
                       tz-f.depth*.5f<z*roomD+.35f||tz+f.depth*.5f>(z+1)*roomD-.35f)continue;
                    bool free=true;
                    for(const auto& prev:cores)if(tx-f.width*.5f<prev.x1+.35f&&tx+f.width*.5f>prev.x0-.35f&&
                        tz-f.depth*.5f<prev.z1+.35f&&tz+f.depth*.5f>prev.z0-.35f)free=false;
                    if(free){cx=tx;cz=tz;found=true;}
                }
                if(!found){graph.error="No occupied room fits the vertical core";return graph;}
            }
            const float x0=cx-f.width*0.5f,x1=cx+f.width*0.5f;
            const float z0=cz-f.depth*0.5f,z1=cz+f.depth*0.5f;
            if(x0<0.35f||x1>W-0.35f||z0<0.35f||z1>D-0.35f){
                graph.error="Vertical core must stay inside the structural perimeter";
                return graph;
            }
            const uint32_t ix=std::min(uint32_t(cx/roomW),recipe.roomsX-1);
            const uint32_t iz=std::min(uint32_t(cz/roomD),recipe.roomsZ-1);
            if(!coreCell(int(ix),int(iz))||x0<float(ix)*roomW+0.35f||x1>float(ix+1)*roomW-0.35f||
               z0<float(iz)*roomD+0.35f||z1>float(iz+1)*roomD-0.35f){
                graph.error="Vertical core must fit inside one room without crossing partitions";
                return graph;
            }
            for(const auto& previous:cores)
                if(x0<previous.x1+0.35f&&x1>previous.x0-0.35f&&
                   z0<previous.z1+0.35f&&z1>previous.z0-0.35f){
                    graph.error="Vertical cores overlap or lack separating supports";
                    return graph;
                }
            cores.push_back({f.kind,i,x0,z0,x1,z1,ix,iz});
            continue;
        }
        const uint32_t side=uint32_t(f.side);
        const uint32_t outer=side==1?recipe.roomsZ:side==3?recipe.roomsX:0;
        const uint32_t line=f.wallLine<0?outer:uint32_t(f.wallLine);
        auto& row=walls[perFloor?f.floor:0][side][line];
        for(uint32_t j=0;j<f.count;++j)row.push_back({i,j,f});
    }
    for(uint32_t floor=0;floor<(perFloor?recipe.floors:1u);++floor)
      for(uint32_t side=0;side<4;++side)for(uint32_t line=0;line<=(side<2?recipe.roomsZ:recipe.roomsX);++line){
        auto& row=walls[floor][side][line];
        const float wallBottom=perFloor?floorTop+storey*float(floor):floorTop;
        const float wallTop=perFloor&&floor+1<recipe.floors?wallBottom+storey:H;
        const uint32_t normalCount=side<2?recipe.roomsZ:recipe.roomsX;
        const uint32_t outer=side==1||side==3?normalCount:0;
        const float plane=line==normalCount?(side<2?D:W):line*(side<2?roomD:roomW);
        const float length=side<2?W:D,margin=0.72f,clearance=0.35f;
        std::vector<std::array<float,2>> spans;
        const uint32_t count=side<2?recipe.roomsX:recipe.roomsZ;
        const float cell=side<2?roomW:roomD;
        for(uint32_t i=0;i<count;++i){
            const int l=int(line),j=int(i);
            const bool active=side==0?occupiedCell(j,l,floor)&&!occupiedCell(j,l-1,floor):
                side==1?occupiedCell(j,l-1,floor)&&!occupiedCell(j,l,floor):
                side==2?occupiedCell(l,j,floor)&&!occupiedCell(l-1,j,floor):
                        occupiedCell(l-1,j,floor)&&!occupiedCell(l,j,floor);
            if(!active)continue;
            const float a=i*cell,b=(i+1==count)?length:(i+1)*cell;
            if(!spans.empty()&&std::abs(spans.back()[1]-a)<.001f)spans.back()[1]=b;
            else spans.push_back({a,b});
        }
        if(spans.empty()){if(!row.empty()){graph.error="Selected facade grid line has no exposed wall";return graph;}continue;}
        const auto fitsSpan=[&](float a,float b,float pad){
            for(const auto& span:spans)if(a>=span[0]+pad-.0001f&&b<=span[1]-pad+.0001f)return true;
            return false;
        };
        // Place exact groups first; reserve their full footprint including
        // minimum structural piers before packing automatic properties.
        for(auto& hole:row)if(hole.value.position>=0){
            const auto& f=hole.value;
            const float span=float(f.count)*f.width+float(f.count-1)*f.spacing;
            hole.start=f.position-span*0.5f+float(hole.copy)*(f.width+f.spacing);
            hole.end=hole.start+f.width;
            hole.placed=true;
            if((shaped||line!=outer)&&!fitsSpan(hole.start,hole.end,margin)){
                graph.error="Explicit opening crosses a footprint recess or corner";return graph;
            }
        }
        auto byStart=[](const Hole& a,const Hole& b){
            if(a.start!=b.start)return a.start<b.start;
            if(a.feature!=b.feature)return a.feature<b.feature;
            return a.copy<b.copy;
        };
        std::vector<Hole> occupied;
        for(const auto& hole:row)if(hole.placed)occupied.push_back(hole);
        std::sort(occupied.begin(),occupied.end(),byStart);
        for(size_t i=1;i<occupied.size();++i)
            if(occupied[i].start-occupied[i-1].end<clearance-0.0001f){
                graph.error="Explicit openings overlap or leave too little pier on "+
                            std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                return graph;
            }
        // When all are automatic, preserve the older evenly spaced layout.
        if(!shaped&&line==outer&&occupied.empty()&&!row.empty()){
            float needed=0;
            for(const auto& hole:row)needed+=hole.value.width;
            const float gap=(length-2*margin-needed)/float(row.size()+1);
            if(gap<clearance){
                graph.error="Entrances cannot fit on "+
                            std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                return graph;
            }
            float cursor=margin+gap;
            for(auto& hole:row){hole.start=cursor;hole.end=cursor+hole.value.width;
                hole.placed=true;cursor=hole.end+gap;}
        }else{
            // Mixed automatic/exact: deterministic first-fit into the free
            // intervals. No auto feature can displace an explicit socket.
            for(auto& hole:row){
                if(hole.placed)continue;
                bool inserted=false;
                std::sort(occupied.begin(),occupied.end(),byStart);
                for(const auto& span:spans){
                    float cursor=span[0]+margin;
                    for(size_t j=0;j<=occupied.size();++j){
                        const float end=std::min(span[1]-margin,j<occupied.size()?occupied[j].start-clearance:length-margin);
                        if(cursor+hole.value.width<=end+0.0001f){
                            hole.start=cursor;hole.end=cursor+hole.value.width;
                            hole.placed=true;occupied.push_back(hole);inserted=true;break;
                        }
                        if(j<occupied.size())cursor=std::max(cursor,occupied[j].end+clearance);
                    }
                    if(inserted)break;
                }
                if(!inserted){
                    graph.error="No free wall span for automatic opening on "+
                                std::string(buildingFeatureSideName(BuildingFeatureSide(side)));
                    return graph;
                }
            }
        }
        std::sort(row.begin(),row.end(),byStart);
        // A partially open door leaf must retract into the usable side wall,
        // never protrude beyond the recipe's exact final model envelope.
        for(const auto& hole:row){
            if(hole.value.open>=0.9999f)continue; // leaves are fully retracted
            const float retract=hole.value.width*0.5f*hole.value.open;
            if(hole.start-retract<0.f||hole.end+retract>length||
               ((shaped||line!=outer)&&!fitsSpan(hole.start-retract,hole.end+retract,0.f))){
                graph.error="Door leaves cannot retract within the requested wall span";
                return graph;
            }
        }
        const auto wallSection=[&](BuildingGraphPart type,float a,float b,
                                    float bottom,float top,uint32_t owner=UINT32_MAX){
            if(b-a<0.0001f||top-bottom<0.0001f)return;
            // Window panes are a thin separate solid, not an opaque
            // full-thickness wall painted like glass.
            const float t=type==BuildingGraphPart::WindowPane?0.05f:wall;
            const float inset=(wall-t)*0.5f;
            for(const auto& span:spans){
            const float lo=std::max(a,span[0]),hi=std::min(b,span[1]);
            if(hi-lo<.0001f)continue;
            switch(BuildingFeatureSide(side)){
              case BuildingFeatureSide::Front:solid(type,lo,bottom,plane+inset,hi,top,plane+inset+t,owner);break;
              case BuildingFeatureSide::Back:solid(type,lo,bottom,plane-inset-t,hi,top,plane-inset,owner);break;
              case BuildingFeatureSide::Left:solid(type,plane+inset,bottom,lo,plane+inset+t,top,hi,owner);break;
              case BuildingFeatureSide::Right:solid(type,plane-inset-t,bottom,lo,plane-inset,top,hi,owner);break;
            }
            }
        };
        float cursor=0.f;
        for(const auto& hole:row){
            const bool upperDoor=hole.value.floor>0&&hole.value.kind==BuildingFeatureKind::PersonnelDoor;
            const bool balcony=upperDoor&&hole.value.balconyDepth>0.f;
            if(balcony){
                const auto& f=hole.value;
                const float center=(hole.start+hole.end)*.5f;
                const int j=int(center/cell),l=int(line);
                const int tx=side<2?j:(side==2?l-1:l);
                const int tz=side<2?(side==0?l-1:l):j;
                const float a=hole.start-.6f,b=hole.end+.6f;
                const float normalSize=side<2?roomD:roomW;
                if(tx<0||tz<0||tx>=int(recipe.roomsX)||tz>=int(recipe.roomsZ)||
                   a<j*cell+.2f||b>(j+1)*cell-.2f||f.balconyDepth>normalSize-.4f){
                    graph.error="Balcony must fit inside one recessed facade cell with side clearance";return graph;
                }
                for(uint32_t k=0;k<recipe.floors;++k)if(occupiedCell(tx,tz,k)){
                    graph.error="Balcony projection requires a clear void on every floor";return graph;
                }
                for(const auto& prev:graph.balconies)if(prev.x==uint32_t(tx)&&prev.z==uint32_t(tz)){
                    graph.error="Only one balcony is supported per void column";return graph;
                }
                // A connected deck touches the facade, with solid parapets
                // on its three free edges. Supports remain in the void.
                const float direction=(side==0||side==2)?-1.f:1.f;
                const float tip=plane+direction*f.balconyDepth;
                const float lo=std::min(plane,tip),hi=std::max(plane,tip);
                const float y=floorTop+storey*float(f.floor)+slabThick;
                auto prism=[&](BuildingGraphPart part,float u0,float v0,float y0,float u1,float v1,float y1){
                    if(side<2)solid(part,u0,y0,v0,u1,y1,v1,hole.feature);
                    else solid(part,v0,y0,u0,v1,y1,u1,hole.feature);
                };
                prism(BuildingGraphPart::Floor,a,lo,y-slabThick,b,hi,y);
                constexpr float rail=.14f;
                prism(BuildingGraphPart::Trim,a,lo,y,a+rail,hi,y+1.1f);
                prism(BuildingGraphPart::Trim,b-rail,lo,y,b,hi,y+1.1f);
                const float railLo=direction<0?tip:tip-rail;
                prism(BuildingGraphPart::Trim,a,railLo,y,b,railLo+rail,y+1.1f);
                for(float u:{a+.24f,b-.24f}){
                    if(f.balconySupport==BuildingBalconySupport::Columns){
                        const float v=tip-direction*.24f;
                        prism(BuildingGraphPart::Trim,u-.12f,v-.12f,0.f,u+.12f,v+.12f,y-slabThick);
                    }else{
                        // Horizontal rectangular support beam seated into the host wall.
                        const float seat=plane-direction*.18f;
                        const float end=tip-direction*.12f;
                        prism(BuildingGraphPart::Trim,u-.12f,std::min(seat,end),y-slabThick-.24f,
                            u+.12f,std::max(seat,end),y-slabThick);
                    }
                }
                graph.balconies.push_back({"entrance/"+std::to_string(hole.feature)+"/"+std::to_string(hole.copy),
                    f.floor,uint32_t(tx),uint32_t(tz),side<2?a:lo,y,side<2?lo:a,side<2?b:hi,side<2?hi:b,f.balconySupport});
            }
            if(upperDoor&&!balcony){
                // Every cell overlapped by the opening must have a real deck on the exterior side.
                bool found=false;
                for(uint32_t i=0;i<count;++i){
                    if(std::min(hole.end,(i+1)*cell)-std::max(hole.start,i*cell)<=.0001f)continue;
                    const int l=int(line),j=int(i);
                    const int tx=side<2?j:(side==2?l-1:l);
                    const int tz=side<2?(side==0?l-1:l):j;
                    if(!functionalTerraceCell(recipe,tx,tz,floor)){
                        graph.error="Upper door opens onto an unsupported or enclosed cell";return graph;
                    }
                    found=true;
                }
                if(!found){graph.error="Upper door has no terrace landing";return graph;}
            }
            wallSection(BuildingGraphPart::Wall,cursor,hole.start,wallBottom,wallTop);
            const bool window=hole.value.kind==BuildingFeatureKind::Window;
            const float bottom=floorTop+storey*float(hole.value.floor)+
                (hole.value.floor?slabThick:0.f)+(window?hole.value.sill:0.f);
            const float top=bottom+hole.value.height;
            if(top+.15f>wallTop+.0001f){graph.error="Opening frame exceeds its storey wall";return graph;}
            if(window||upperDoor)wallSection(BuildingGraphPart::Wall,hole.start,hole.end,wallBottom,bottom);
            wallSection(BuildingGraphPart::Wall,hole.start,hole.end,top,wallTop);
            constexpr float frame=0.14f;
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.start,bottom,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.end,hole.end+frame,bottom,top,hole.feature);
            wallSection(BuildingGraphPart::Trim,hole.start-frame,hole.end+frame,top,top+0.15f,hole.feature);
            if(window){
                wallSection(BuildingGraphPart::WindowPane,hole.start+0.06f,hole.end-0.06f,
                            bottom+0.06f,top-0.06f,hole.feature);
            }else{
                const float half=hole.value.width*0.5f,shift=half*hole.value.open;
                if(hole.value.open<0.9999f){
                    wallSection(BuildingGraphPart::DoorLeaf,hole.start-shift,hole.start+half-shift,
                                bottom+0.05f,top-0.05f,hole.feature);
                    wallSection(BuildingGraphPart::DoorLeaf,hole.end-half+shift,hole.end+shift,
                                bottom+0.05f,top-0.05f,hole.feature);
                }
            }
            const float longitudinal=(hole.start+hole.end)*0.5f;
            float px=longitudinal,pz=plane-1.f;
            if(side==1)pz=plane+1.f;
            if(side==2){px=plane-1.f;pz=longitudinal;}
            if(side==3){px=plane+1.f;pz=longitudinal;}
            graph.sockets.push_back({(window?"window/":"entrance/")+std::to_string(hole.feature)+"/"+
                std::to_string(hole.copy),hole.value.kind,BuildingFeatureSide(side),
                hole.feature,hole.copy,px,window||upperDoor?bottom:0.f,pz,
                hole.value.width,hole.value.height,0.f,hole.value.open,plane,hole.value.floor,bottom});
            cursor=hole.end;
        }
        wallSection(BuildingGraphPart::Wall,cursor,length,wallBottom,wallTop);
    }
    // A room is an accessible rectangular cell, not merely a decorative
    // rectangle. Real internal partitions are segmented around each opening.
    // Connections record room adjacency for later navmesh/door integrations.
    const float cellW=W/float(recipe.roomsX),cellD=D/float(recipe.roomsZ);
    auto roomId=[](uint32_t f,uint32_t x,uint32_t z){
        return std::string("room/")+std::to_string(f)+"/"+
            std::to_string(x)+"/"+std::to_string(z);
    };
    for(uint32_t f=0;f<recipe.floors;++f){
        const float level=floorTop+storey*float(f);
        const float bottom=level+(f?slabThick:0.f);
        const float ceiling=std::min(H-roofThickness,level+storey);
        const float lintel=std::min(bottom+2.2f,ceiling-0.2f);
        for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x)
            if(occupiedCell(int(x),int(z),f))graph.rooms.push_back({roomId(f,x,z),f,x,z,float(x)*cellW,bottom,
                float(z)*cellD,float(x+1)*cellW,ceiling,float(z+1)*cellD});
        for(uint32_t x=1;x<recipe.roomsX;++x)for(uint32_t z=0;z<recipe.roomsZ;++z){
            if(!occupiedCell(int(x)-1,int(z),f)||!occupiedCell(int(x),int(z),f))continue;
            const float at=float(x)*cellW,mid=(float(z)+0.5f)*cellD;
            const float a=mid-0.6f,c=mid+0.6f;
            solid(BuildingGraphPart::Partition,at-0.12f,bottom,float(z)*cellD,
                  at+0.12f,ceiling,a);
            solid(BuildingGraphPart::Partition,at-0.12f,bottom,c,
                  at+0.12f,ceiling,float(z+1)*cellD);
            solid(BuildingGraphPart::Partition,at-0.12f,lintel,a,
                  at+0.12f,ceiling,c);
            graph.connections.push_back({"passage/x/"+std::to_string(f)+"/"+
                std::to_string(x)+"/"+std::to_string(z),roomId(f,x-1,z),
                roomId(f,x,z),f,at,bottom,mid,1.2f,lintel-bottom});
        }
        for(uint32_t z=1;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x){
            if(!occupiedCell(int(x),int(z)-1,f)||!occupiedCell(int(x),int(z),f))continue;
            const float at=float(z)*cellD,mid=(float(x)+0.5f)*cellW;
            const float a=mid-0.6f,c=mid+0.6f;
            solid(BuildingGraphPart::Partition,float(x)*cellW,bottom,at-0.12f,
                  a,ceiling,at+0.12f);
            solid(BuildingGraphPart::Partition,c,bottom,at-0.12f,
                  float(x+1)*cellW,ceiling,at+0.12f);
            solid(BuildingGraphPart::Partition,a,lintel,at-0.12f,
                  c,ceiling,at+0.12f);
            graph.connections.push_back({"passage/z/"+std::to_string(f)+"/"+
                std::to_string(x)+"/"+std::to_string(z),roomId(f,x,z-1),
                roomId(f,x,z),f,mid,bottom,at,1.2f,lintel-bottom});
        }
    }
    // Wall sockets carry the exact authored opening centre and width. Clip
    // the opening interval against every boundary cell; a wide gate may
    // overlap more than one room. Do not add exterior shortcuts to room routes.
    for(const auto& socket:graph.sockets){
        if(socket.kind!=BuildingFeatureKind::PersonnelDoor&&
           socket.kind!=BuildingFeatureKind::SmallGate&&
           socket.kind!=BuildingFeatureKind::LargeGate)continue;
        const bool alongX=socket.side==BuildingFeatureSide::Front||
                          socket.side==BuildingFeatureSide::Back;
        const float center=alongX?socket.x:socket.z;
        for(const auto& room:graph.rooms){
            if(room.floor!=socket.floor)continue;
            const float plane=buildingSocketWallPlane(socket,W,D);
            const bool boundary=socket.side==BuildingFeatureSide::Front?
                std::abs(room.z0-plane)<.001f&&!occupiedCell(int(room.x),int(room.z)-1,room.floor):
                socket.side==BuildingFeatureSide::Back?
                std::abs(room.z1-plane)<.001f&&!occupiedCell(int(room.x),int(room.z)+1,room.floor):
                socket.side==BuildingFeatureSide::Left?
                std::abs(room.x0-plane)<.001f&&!occupiedCell(int(room.x)-1,int(room.z),room.floor):
                std::abs(room.x1-plane)<.001f&&!occupiedCell(int(room.x)+1,int(room.z),room.floor);
            if(!boundary)continue;
            const float a=std::max(center-socket.width*0.5f,alongX?room.x0:room.z0);
            const float b=std::min(center+socket.width*0.5f,alongX?room.x1:room.z1);
            if(b-a>0.001f){
                if(socket.floor==0)graph.exteriorApertures.push_back({socket.id,room.id,a,b});
                else{
                    const int tx=int(room.x)+(socket.side==BuildingFeatureSide::Left?-1:socket.side==BuildingFeatureSide::Right?1:0);
                    const int tz=int(room.z)+(socket.side==BuildingFeatureSide::Front?-1:socket.side==BuildingFeatureSide::Back?1:0);
                    const bool balcony=std::any_of(graph.balconies.begin(),graph.balconies.end(),[&](const auto& deck){
                        return deck.socketId==socket.id&&int(deck.x)==tx&&int(deck.z)==tz;
                    });
                    if(functionalTerraceCell(recipe,tx,tz,room.floor)||balcony)
                        graph.terraceAccess.push_back({socket.id,room.id,room.floor,uint32_t(tx),uint32_t(tz)});
                }
            }
        }
    }
    // Cut rectangular shafts from EVERY upper slab using a bounded planar
    // partition. Each emitted floor box remains a real collision solid.
    for(uint32_t floor=1;floor<recipe.floors;++floor){
        const float y=floorTop+storey*float(floor);
        std::vector<float> xs{0.f,W},zs{0.f,D};
        if(shaped){for(uint32_t x=1;x<recipe.roomsX;++x)xs.push_back(x*roomW);
            for(uint32_t z=1;z<recipe.roomsZ;++z)zs.push_back(z*roomD);}
        for(const auto& core:cores){
            xs.push_back(core.x0);xs.push_back(core.x1);
            zs.push_back(core.z0);zs.push_back(core.z1);
        }
        std::sort(xs.begin(),xs.end());std::sort(zs.begin(),zs.end());
        xs.erase(std::unique(xs.begin(),xs.end()),xs.end());
        zs.erase(std::unique(zs.begin(),zs.end()),zs.end());
        for(size_t ix=1;ix<xs.size();++ix)for(size_t iz=1;iz<zs.size();++iz){
            const float mx=(xs[ix-1]+xs[ix])*0.5f,mz=(zs[iz-1]+zs[iz])*0.5f;
            bool hole=false;
            for(const auto& core:cores)
                if(mx>core.x0&&mx<core.x1&&mz>core.z0&&mz<core.z1){hole=true;break;}
            if(!hole&&(!shaped||occupiedPoint(mx,mz,floor)))solid(BuildingGraphPart::Floor,xs[ix-1],y,zs[iz-1],
                           xs[ix],y+slabThick,zs[iz]);
            else if(perFloor&&occupiedPoint(mx,mz,floor-1)&&!occupiedPoint(mx,mz,floor))
                solid(recipe.walkableSetbacks?BuildingGraphPart::Floor:BuildingGraphPart::Roof,
                    xs[ix-1],y,zs[iz-1],xs[ix],y+slabThick,zs[iz]);
        }
    }
    // Supported roof decks are real Floor solids so the existing static
    // navigation source can sample them. Parapets are collidable Trim solids.
    // No parapet along an interior wall or across adjacent deck cells.
    if(recipe.walkableSetbacks)for(uint32_t floor=1;floor<recipe.floors;++floor){
        const float y=floorTop+storey*float(floor)+slabThick;
        constexpr float rail=.14f,railHeight=1.1f;
        for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x){
            if(!functionalTerraceCell(recipe,int(x),int(z),floor))continue;
            const float x0=x*roomW,x1=(x+1)*roomW,z0=z*roomD,z1=(z+1)*roomD;
            graph.terraces.push_back({floor,x,z,x0,y,z0,x1,z1});
            const auto edge=[&](int nx,int nz){
                return !occupiedCell(nx,nz,floor)&&!functionalTerraceCell(recipe,nx,nz,floor);
            };
            if(edge(int(x)-1,int(z)))solid(BuildingGraphPart::Trim,x0,y,z0,x0+rail,y+railHeight,z1);
            if(edge(int(x)+1,int(z)))solid(BuildingGraphPart::Trim,x1-rail,y,z0,x1,y+railHeight,z1);
            if(edge(int(x),int(z)-1))solid(BuildingGraphPart::Trim,x0,y,z0,x1,y+railHeight,z0+rail);
            if(edge(int(x),int(z)+1))solid(BuildingGraphPart::Trim,x0,y,z1-rail,x1,y+railHeight,z1);
        }
    }
    // Model two opposite stairs flights with a turn landing. Treads are
    // physical boxes; semantic vertical links remain separate from navmesh.
    auto roomIdFor=[](uint32_t floor,uint32_t x,uint32_t z){
        return std::string("room/")+std::to_string(floor)+"/"+
            std::to_string(x)+"/"+std::to_string(z);
    };
    for(const auto& core:cores){
        const float cx=(core.x0+core.x1)*0.5f,cz=(core.z0+core.z1)*0.5f;
        if(core.kind==BuildingFeatureKind::Elevator){
            // Slim fixed guide rails make the lift-shaft footprint visible
            // without filling its movement volume with a solid collider.
            constexpr float e=0.09f;
            for(const float x:{core.x0+0.12f,core.x1-0.12f})
                for(const float z:{core.z0+0.12f,core.z1-0.12f})
                    solid(BuildingGraphPart::ElevatorGuide,x-e,floorTop,z-e,
                          x+e,H-roofThickness,z+e,core.feature);
        }
        for(uint32_t floor=0;floor+1<recipe.floors;++floor){
            const float bottom=floorTop+storey*float(floor)+(floor?slabThick:0.f);
            const float next=floorTop+storey*float(floor+1)+slabThick;
            const float climb=next-bottom;
            const std::string from=roomIdFor(floor,core.roomX,core.roomZ);
            const std::string to=roomIdFor(floor+1,core.roomX,core.roomZ);
            const std::string token=std::to_string(core.feature)+"/"+std::to_string(floor);
            graph.connections.push_back({"vertical/"+token,from,to,floor,cx,bottom,cz,
                core.x1-core.x0,climb,
                core.kind==BuildingFeatureKind::Stairs?
                    BuildingConnectionKind::Stair:BuildingConnectionKind::Elevator});
            graph.sockets.push_back({(core.kind==BuildingFeatureKind::Stairs?"stairs/":"lift/")+token,
                core.kind,BuildingFeatureSide::Front,core.feature,floor,cx,bottom,cz,
                core.x1-core.x0,climb,core.z1-core.z0,0.f});
            if(core.kind==BuildingFeatureKind::Elevator){
                // ONE cabin floor at the initial station. It must not make
                // a fixed blocking slab on EVERY upper storey. Moving the
                // platform and doors is a separate authoritative runtime
                // mechanism, not simulated by the static geometry recipe.
                if(floor==0)solid(BuildingGraphPart::ElevatorPlatform,
                    core.x0,bottom,core.z0,core.x1,bottom+0.18f,core.z1,core.feature);
                continue;
            }
            const float mid=bottom+climb*0.5f;
            const float lane=(core.x1-core.x0-0.18f)*0.5f;
            const float halfRun=core.z1-core.z0-0.9f;
            const int steps=std::max(8,int(std::ceil(climb/0.18f)));
            const int perFlight=(steps+1)/2;
            const float rise=climb/float(2*perFlight);
            for(int j=0;j<perFlight;++j){
                const float z0=core.z0+halfRun*float(j)/float(perFlight);
                const float z1=core.z0+halfRun*float(j+1)/float(perFlight);
                const float y0=bottom+rise*float(j),y1=bottom+rise*float(j+1);
                solid(BuildingGraphPart::StairTread,core.x0,y0,z0,
                      core.x0+lane,y1,z1,core.feature);
                // Return flight climbs towards -Z after the turn landing.
                const float back0=core.z0+halfRun*float(perFlight-j-1)/float(perFlight);
                const float back1=core.z0+halfRun*float(perFlight-j)/float(perFlight);
                const float by0=mid+rise*float(j),by1=mid+rise*float(j+1);
                solid(BuildingGraphPart::StairTread,core.x1-lane,by0,back0,
                      core.x1,by1,back1,core.feature);
            }
            solid(BuildingGraphPart::StairLanding,core.x0,mid-0.14f,
                  core.z1-0.9f,core.x1,mid,core.z1,core.feature);
        }
    }
    const float roofBase=H-roofThickness;
    if(!roof){
        footprintSlab(BuildingGraphPart::Roof,roofBase,H,recipe.floors-1);
        const auto profile=recipe.roofProfile;
        if(shaped&&profile!=FunctionalRoofProfile::Flat){
            // Cover the top occupied mask by disjoint maximal rectangles.
            // Deterministic row-major anchor, largest area then longest X.
            // Unlike an envelope roof, no section bridges a courtyard.
            std::array<bool,36> used{};
            const uint32_t topFloor=recipe.floors-1;
            const auto available=[&](uint32_t x,uint32_t z){
                return occupiedCell(int(x),int(z),topFloor)&&!used[z*6+x];
            };
            for(uint32_t z=0;z<recipe.roomsZ;++z)for(uint32_t x=0;x<recipe.roomsX;++x){
                if(!available(x,z))continue;
                uint32_t bestX=1,bestZ=1;
                for(uint32_t dz=1;z+dz<=recipe.roomsZ;++dz)for(uint32_t dx=1;x+dx<=recipe.roomsX;++dx){
                    if(dx*dz<bestX*bestZ||(dx*dz==bestX*bestZ&&dx<=bestX))continue;
                    bool fit=true;
                    for(uint32_t j=z;j<z+dz;++j)for(uint32_t i=x;i<x+dx;++i)
                        if(!available(i,j))fit=false;
                    if(fit){bestX=dx;bestZ=dz;}
                }
                for(uint32_t j=z;j<z+bestZ;++j)for(uint32_t i=x;i<x+bestX;++i)used[j*6+i]=true;
                const float ax=x*roomW,az=z*roomD;
                const float bx=x+bestX==recipe.roomsX?W:(x+bestX)*roomW;
                const float bz=z+bestZ==recipe.roomsZ?D:(z+bestZ)*roomD;
                const float w=bx-ax,d=bz-az;
                const bool alongX=w>d;
                graph.roofSections.push_back({ax,az,bx,bz,H,recipe.roofRise,alongX});
                if(profile==FunctionalRoofProfile::Terrace||profile==FunctionalRoofProfile::Pagoda){
                    const int levels=profile==FunctionalRoofProfile::Pagoda?3:4;
                    const float dy=recipe.roofRise/levels;
                    for(int k=0;k<levels;++k){
                        const float inset=.095f*k;
                        solid(BuildingGraphPart::Roof,ax+w*inset,H+dy*k,az+d*inset,
                            bx-w*inset,H+dy*(k+1),bz-d*inset);
                    }
                    continue;
                }
                const bool twoAxis=profile==FunctionalRoofProfile::Dome||profile==FunctionalRoofProfile::Hip;
                const int nx=twoAxis||!alongX?12:1,nz=twoAxis||alongX?12:1;
                auto height=[&](int i,int j){
                    const float u=2.f*i/nx-1.f,v=2.f*j/nz-1.f;
                    return H+recipe.roofRise*functionalRoofFraction(profile,alongX?v:u,alongX?u:v);
                };
                for(int j=0;j<nz;++j)for(int i=0;i<nx;++i){
                    const std::array<float,4> corners{height(i,j),height(i+1,j),height(i+1,j+1),height(i,j+1)};
                    const float peak=*std::max_element(corners.begin(),corners.end());
                    if(peak<=H+.0001f)continue;
                    const size_t before=graph.solids.size();
                    solid(BuildingGraphPart::Roof,ax+w*i/nx,H,az+d*j/nz,
                        ax+w*(i+1)/nx,peak,az+d*(j+1)/nz);
                    if(graph.solids.size()!=before)graph.solids.back().roofTop=corners;
                }
            }
        }else if(profile==FunctionalRoofProfile::Terrace||profile==FunctionalRoofProfile::Pagoda){
            const int levels=profile==FunctionalRoofProfile::Pagoda?3:4;
            const float dy=recipe.roofRise/levels;
            for(int level=0;level<levels;++level){
                const float f=1.f-.19f*level;
                const float x=W*(1-f)*.5f,z=D*(1-f)*.5f;
                solid(BuildingGraphPart::Roof,x,H+dy*level,z,W-x,H+dy*(level+1),D-z);
            }
        }else if(profile!=FunctionalRoofProfile::Flat){
            constexpr int nx=12;
            const int nz=(profile==FunctionalRoofProfile::Dome||profile==FunctionalRoofProfile::Hip)?12:1;
            auto norm=[](int i,int n){return 2.f*i/n-1.f;};
            for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){
                float peak=0.f;
                for(int dz:{0,1})for(int dx:{0,1})
                    peak=std::max(peak,functionalRoofFraction(profile,norm(x+dx,nx),norm(z+dz,nz)));
                // Collision is a conservative per-cell prism; the visible
                // surface is clipped to the same cells by the mesh owner.
                if(peak>0.f)solid(BuildingGraphPart::Roof,W*x/nx,H,D*z/nz,
                    W*(x+1)/nx,H+recipe.roofRise*peak,D*(z+1)/nz);
            }
        }
    }
    else {
        const float cx=roof->position>=0?roof->position:W*0.5f;
        const float cz=roof->positionZ>=0?roof->positionZ:D*0.5f;
        const float x0=cx-roof->width*0.5f,x1=cx+roof->width*0.5f;
        const float z0=cz-roof->depth*0.5f,z1=cz+roof->depth*0.5f;
        solid(BuildingGraphPart::Roof,0,roofBase,0,x0,H,D);
        solid(BuildingGraphPart::Roof,x1,roofBase,0,W,H,D);
        solid(BuildingGraphPart::Roof,x0,roofBase,0,x1,H,z0);
        solid(BuildingGraphPart::Roof,x0,roofBase,z1,x1,H,D);
        const float half=roof->width*0.5f,travel=half*roof->open;
        solid(BuildingGraphPart::RoofPanel,x0-travel,roofBase,z0,
              x0+half-travel,H,z1,roofIndex);
        solid(BuildingGraphPart::RoofPanel,x0+half+travel,roofBase,z0,
              x1+travel,H,z1,roofIndex);
        graph.sockets.push_back({"roof/"+std::to_string(roofIndex),
            BuildingFeatureKind::SlidingRoof,roof->side,roofIndex,0,
            cx,H,cz,roof->width,0.f,roof->depth,roof->open});
    }
    if(std::any_of(recipe.cornerChamfers.begin(),recipe.cornerChamfers.end(),[](float c){return c>0.f;}))
        buildingChamferDetail::apply(graph,recipe);
    return graph;
}
} // namespace aiverse::lib::systems
