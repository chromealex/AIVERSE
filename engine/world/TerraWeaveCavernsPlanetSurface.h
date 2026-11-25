#pragma once
// Canonical coarse simulation surface, no synthetic flat Earthworks terrain.
// A native render/detail owner can supply its actual selected triangles to
// the same projection adapter, but must prove matching collision topology.
#include "engine/world/TerraWeaveCavernsPlanetQueries.h"
#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include <set>
#include <span>
#include <tuple>

namespace aiverse::terraweave::caverns {
struct CavePlanetSurfaceCell {
    int face=0,x=0,z=0;
};
struct CavePlanetSurfacePatch {
    std::vector<world::Triangle> chartTriangles;
    uint64_t surfaceRevision=0;
    CavePlanetAnchor anchor{};
    // Parallel to chartTriangles; two outward-wound source triangles per cell.
    // Keep exact body points rather than reconstructing them from a rounded chart.
    std::vector<world::Triangle> bodyTriangles;
    std::vector<CavePlanetSurfaceCell> cells;
};
inline world::Triangle projectPlanetSurfaceTriangle(const CavePlanetFrame& frame,
        const world::Triangle& bodyLocal){
    world::Triangle t{frame.fromBody(bodyLocal.a),frame.fromBody(bodyLocal.b),frame.fromBody(bodyLocal.c)};
    const auto n=fxCross(t.b-t.a,t.c-t.a);
    if(n.y<=Fixed{}||!world::SpatialWorld::validTriangle(t))
        throw std::invalid_argument("Caverns planetary surface is not an upward chart heightfield");
    return t;
}
inline CavePlanetSurfacePatch captureCavePlanetSurface(const CavePlanetFrame& frame,
        std::span<const CavePlanetSurfaceCell> cells,uint64_t surfaceRevision){
    if(!surfaceRevision||cells.empty()||cells.size()>32767)
        throw std::invalid_argument("Caverns planetary terrain capture budget");
    const auto& s=frame.surface();const int body=frame.body();
    const auto n=s.bodies[size_t(body)].faceSize;
    std::set<std::tuple<int,int,int>> unique;
    CavePlanetSurfacePatch out;out.surfaceRevision=surfaceRevision;out.anchor=frame.anchor();
    out.chartTriangles.reserve(cells.size()*2);
    out.bodyTriangles.reserve(cells.size()*2);
    out.cells.reserve(cells.size());
    for(const auto& cell:cells){
        if(cell.face<0||cell.face>=6||cell.x<0||cell.z<0||cell.x>=n||cell.z>=n||
           !unique.emplace(cell.face,cell.x,cell.z).second)
            throw std::invalid_argument("Caverns planetary terrain cell");
        const auto point=[&](int dx,int dz){
            return s.facePoint(body,cell.face,Fixed::fromInt(cell.x+dx),Fixed::fromInt(cell.z+dz));
        };
        const auto a=point(0,0),b=point(1,0),c=point(1,1),d=point(0,1);
        // Same diagonal as the cube-sphere surface renderer. Orient
        // outward explicitly (all faces must work, including negative Y).
        const auto append=[&](world::Triangle t){
            if(fxCross(t.b-t.a,t.c-t.a).dot(t.a)<Fixed{})std::swap(t.b,t.c);
            out.chartTriangles.push_back(projectPlanetSurfaceTriangle(frame,t));
            out.bodyTriangles.push_back(t);
        };
        append({a,c,b});append({a,d,c});
        out.cells.push_back(cell);
    }
    return out;
}
// Sample the selected actual triangle planes, not Surface::heightAt's
// bilinear interpolation. Return no value for missing/ambiguous coverage.
inline std::optional<Fixed> cavePlanetChartHeight(const CavePlanetSurfacePatch& patch,Fixed x,Fixed z){
    std::optional<Fixed> hit;
    const Vec3 p{x,{},z};
    const auto cross2=[](Vec3 a,Vec3 b){return a.x*b.z-a.z*b.x;};
    for(const auto& t:patch.chartTriangles){
        const auto u=t.b-t.a,v=t.c-t.a,w=p-t.a;
        const auto det=cross2(u,v);
        if(det==Fixed{})continue;
        const auto un=cross2(w,v),vn=cross2(u,w);
        if(det<Fixed{}?(un>Fixed{}||vn>Fixed{}||un+vn<det):
                       (un<Fixed{}||vn<Fixed{}||un+vn>det))continue;
        const auto y=t.a.y+u.y*(un/det)+v.y*(vn/det);
        if(hit&&fxAbs(*hit-y)>Fixed::fromRatio(1,100))return {};
        hit=y;
    }
    return hit;
}
// Author a shaft in the same chart using measured surface height and
// existing Entrance semantics. +Y is radial at chart anchor, not world Y.
inline Entrance makeCavePlanetEntrance(const CavePlanetFrame& frame,
        const CavePlanetSurfacePatch& patch,uint64_t id,int64_t xCm,int64_t zCm,
        int32_t depthCm,int32_t radiusCm){
    if(!id||depthCm<1||radiusCm<1||!sameCavePlanetAnchor(patch.anchor,frame.anchor()))
        throw std::invalid_argument("Caverns planetary shaft identity");
    const auto local=PointCm{xCm,0,zCm};
    if(!frame.contains(local))throw std::out_of_range("Caverns planetary shaft outside chart");
    const auto height=cavePlanetChartHeight(patch,Fixed::fromRatio(xCm,100),Fixed::fromRatio(zCm,100));
    if(!height)throw std::invalid_argument("Caverns planetary shaft missing actual surface");
    const auto y=CavePlanetFrame::centimetres({{},*height,{}}).y;
    const auto cap=int64_t(frame.anchor().halfExtentCm);
    if(xCm-radiusCm< -cap||xCm+radiusCm>cap||zCm-radiusCm< -cap||zCm+radiusCm>cap||
       y+radiusCm>cap||y-depthCm-radiusCm< -cap)
        throw std::invalid_argument("Caverns planetary shaft influence outside chart");
    return {id,xCm,zCm,y,y-depthCm,radiusCm};
}
} // namespace aiverse::terraweave::caverns
