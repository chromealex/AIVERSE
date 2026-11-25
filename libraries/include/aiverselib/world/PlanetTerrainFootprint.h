// Read-only seam-aware mutation footprint for the canonical terrain owner.
// Never writes Surface, never estimates physical volume, no new terrain state.
#pragma once
#include "engine/game/Surface.h"
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace aiverse::lib::worldprocess {
struct PlanetTerrainCorner {
    int canonical=-1; // body-local CubeSphere corner ID
    std::array<int,3> aliases{{-1,-1,-1}};
    int aliasCount=0;
    int16_t beforeHeight16=0; // exact Surface corner units (1/16 metre)
};
struct PlanetTerrainFootprint {
    int body=-1;
    std::string bodyId;
    int surfaceCell=-1;
    int faceSize=0;
    std::array<PlanetTerrainCorner,4> corners{};
    // Sorted, unique global grid cells touching any of the four vertices.
    std::vector<int> affectedCells;
};

// Requires an already-built canonical Surface snapshot. Aliases are resolved
// by CubeSphere, not by floating-point positions or world-space proximity.
// Reject inconsistent seam heights rather than silently choosing an authority.
inline std::optional<PlanetTerrainFootprint> planetTerrainFootprint(
        const game::Surface& surface,int body,int cell){
    if(!surface.sphere()||!surface.tab||body<0||
       size_t(body)>=surface.bodies.size()||size_t(body)>=surface.tab->cs.size()||
       cell<0||int64_t(cell)>=int64_t(surface.W)*surface.H)return {};
    const auto& def=surface.bodies[size_t(body)];
    const auto& cube=surface.cubeOf(body);
    const int n=def.faceSize;
    if(n<1||cube.faceSize()!=n||def.id.empty())return {};
    const size_t stride=size_t(n)+1;
    const size_t perFace=stride*stride;
    const size_t count=6*perFace;
    if(def.cornerBase>surface.corners.size()||
       count>surface.corners.size()-def.cornerBase)return {};
    int b=-1,f=-1,x=-1,y=-1;
    if(!surface.cellFace(cell,b,f,x,y)||b!=body||!cube.validCell(f,x,y))return {};
    PlanetTerrainFootprint out;
    out.body=body;out.bodyId=def.id;out.surfaceCell=cell;out.faceSize=n;
    size_t index=0;
    for(int dy=0;dy<=1;++dy)for(int dx=0;dx<=1;++dx){
        auto& corner=out.corners[index++];
        corner.aliasCount=cube.cornerDuplicates(f,x+dx,y+dy,corner.aliases.data());
        if(corner.aliasCount<1||corner.aliasCount>3)return {};
        corner.canonical=corner.aliases[0];
        if(corner.canonical<0||size_t(corner.canonical)>=count)return {};
        corner.beforeHeight16=surface.corners[def.cornerBase+size_t(corner.canonical)];
        for(int i=0;i<corner.aliasCount;++i){
            const int alias=corner.aliases[size_t(i)];
            if(alias<0||size_t(alias)>=count||
               surface.corners[def.cornerBase+size_t(alias)]!=corner.beforeHeight16)return {};
            const int face=int(size_t(alias)/perFace);
            const size_t offset=size_t(alias)%perFace;
            const int cx=int(offset%stride),cy=int(offset/stride);
            for(int oy=-1;oy<=0;++oy)for(int ox=-1;ox<=0;++ox){
                if(!cube.validCell(face,cx+ox,cy+oy))continue;
                out.affectedCells.push_back(surface.gridCell(body,face,cx+ox,cy+oy));
            }
        }
    }
    std::sort(out.corners.begin(),out.corners.end(),[](const auto& a,const auto& b){
        return a.canonical<b.canonical;
    });
    std::sort(out.affectedCells.begin(),out.affectedCells.end());
    out.affectedCells.erase(std::unique(out.affectedCells.begin(),out.affectedCells.end()),out.affectedCells.end());
    return out;
}
} // namespace aiverse::lib::worldprocess
