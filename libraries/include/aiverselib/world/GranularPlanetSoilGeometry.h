// Conservative discrete geometry preview for the existing terrain owner.
// Never publishes Surface or changes ECS. The owner must explicitly adopt
// the centimetre-quantized triangular volume convention before committing.
#pragma once
#include "GranularPlanetTerrainTransfer.h"
#include <limits>
#include <map>
#include <memory>

namespace aiverse::granular {
struct PlanetSoilGeometryPreview {
    std::shared_ptr<const game::Surface> surface;
    std::vector<int> affectedCells;
    uint64_t movedVolumeCm3=0;
    int32_t heightDelta16=0;
};
namespace planet_soil_geometry_detail {
struct Point { int64_t x=0,y=0,z=0; };
inline std::optional<Point> vertex(const game::Surface& s,int body,int face,int x,int y,
                                   int16_t height){
    const auto radius=s.bodies[size_t(body)].radius+Fixed::fromRatio(height,16);
    // Bound before multiplication: each integer determinant fits int64.
    // Also respect the existing body-isolation layout.
    if(radius<=Fixed{}||radius>Fixed::fromInt(1500))return {};
    const auto p=s.cubeOf(body).cornerDir(face,x,y)*radius;
    const auto cm=[](Fixed v){
        const auto raw=v.raw*100;
        return raw>=0?(raw+Fixed::kOne/2)/Fixed::kOne:
            -((-raw+Fixed::kOne/2)/Fixed::kOne);
    };
    Point out{cm(p.x),cm(p.y),cm(p.z)};
    for(auto v:{out.x,out.y,out.z})if(v< -150000||v>150000)return {};
    return out;
}
inline uint64_t tetraVolume(Point a,Point b,Point c){
    // Six times the volume of the tetrahedron with the body centre.
    // Integer cm coordinates; floor each triangle independently so
    // successive changes telescope and reversing an edit restores volume.
    const int64_t det=a.x*(b.y*c.z-b.z*c.y)+
                      a.y*(b.z*c.x-b.x*c.z)+
                      a.z*(b.x*c.y-b.y*c.x);
    return uint64_t(det<0?-det:det)/6;
}
inline std::optional<uint64_t> cellVolume(const game::Surface& s,int body,int cell){
    int b=-1,f=-1,x=-1,y=-1;
    if(!s.cellFace(cell,b,f,x,y)||b!=body)return {};
    const auto a=vertex(s,b,f,x,y,s.corner(b,f,x,y));
    const auto q=vertex(s,b,f,x+1,y,s.corner(b,f,x+1,y));
    const auto c=vertex(s,b,f,x,y+1,s.corner(b,f,x,y+1));
    const auto d=vertex(s,b,f,x+1,y+1,s.corner(b,f,x+1,y+1));
    if(!a||!q||!c||!d)return {};
    return tetraVolume(*a,*q,*d)+tetraVolume(*a,*d,*c);
}
}
// Build ONE bounded uniform corner edit (1..32 height quanta, 1/16 m).
// The exact accounting here is for the specified cm-quantized triangle mesh,
// NOT analytic bilinear Surface volume or TerraWeave's planar Earthworks.
// The caller supplies its authoritative bedrock and height bounds in Surface
// units, and must validate revision/recipe and all dependent owner updates.
// No edit is returned if even this quantum exceeds the mass budget.
inline std::optional<PlanetSoilGeometryPreview> previewPlanetSoilGeometry(
        const game::Surface& source,const PlanetSoilTarget& target,
        int32_t quanta,int16_t minimumHeight16,int16_t maximumHeight16){
    if(quanta<1||quanta>32||minimumHeight16>maximumHeight16||
       !target.volumeBudgetCm3||
       (target.direction!=PlanetSoilDirection::Dig&&target.direction!=PlanetSoilDirection::Fill))return {};
    const auto footprint=lib::worldprocess::planetTerrainFootprint(source,target.body,target.surfaceCell);
    if(!footprint||footprint->bodyId!=target.bodyId||
       target.footprint.bodyId!=target.bodyId||target.footprint.surfaceCell!=target.surfaceCell||
       target.footprint.faceSize!=footprint->faceSize||
       target.footprint.affectedCells!=footprint->affectedCells)return {};
    for(size_t i=0;i<footprint->corners.size();++i){
        const auto& a=footprint->corners[i];const auto& b=target.footprint.corners[i];
        if(a.canonical!=b.canonical||a.aliases!=b.aliases||
           a.aliasCount!=b.aliasCount||a.beforeHeight16!=b.beforeHeight16)return {};
    }
    const int32_t delta=target.direction==PlanetSoilDirection::Fill?quanta:-quanta;
    const size_t base=source.bodies[size_t(target.body)].cornerBase;
    // Snapshot only; topology tables stay shared and immutable.
    auto candidate=std::make_shared<game::Surface>(source);
    for(const auto& corner:footprint->corners){
        const int32_t next=int32_t(corner.beforeHeight16)+delta;
        if(next<minimumHeight16||next>maximumHeight16)return {};
        for(int i=0;i<corner.aliasCount;++i)
            candidate->corners[base+size_t(corner.aliases[size_t(i)])]=int16_t(next);
    }
    uint64_t moved=0;
    for(int cell:footprint->affectedCells){
        const auto before=planet_soil_geometry_detail::cellVolume(source,target.body,cell);
        const auto after=planet_soil_geometry_detail::cellVolume(*candidate,target.body,cell);
        if(!before||!after)return {};
        if(delta>0?*after<*before:*after>*before)return {};
        const auto change=delta>0?*after-*before:*before-*after;
        if(change>target.volumeBudgetCm3-moved)return {};
        moved+=change;
    }
    // A geometry change below the integer volume resolution is not free soil.
    if(!moved)return {};
    return PlanetSoilGeometryPreview{std::move(candidate),footprint->affectedCells,moved,delta};
}
} // namespace aiverse::granular
