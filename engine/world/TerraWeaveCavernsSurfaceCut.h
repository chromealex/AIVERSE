#pragma once
// TerraWeave Caverns surface cut. Produces one common triangle soup for
// visual + collision adapters. Does not publish to SpatialWorld / RHI.
#include "TerraWeaveCavernsEntrances.h"
#include "engine/world/TerraWeaveTerrainPatch.h"
#include <cmath>
#include <array>
#include <utility>
namespace aiverse::terraweave::caverns {
struct CutVertexCm {double x=0,y=0,z=0;};
struct CutTriangleCm {CutVertexCm a{},b{},c{};};
struct CutSurface {
    world::Cell cell{};
    uint64_t sourceRevision=0;
    std::vector<CutTriangleCm> triangles;
    std::vector<SurfaceRectCm> dirtyRegions;
    // An integration owner must publish this single triangle list to BOTH
    // visual and physics and then refresh native navmesh. No automatic commit.
};
inline CutVertexCm toCutVertex(Vec3 p) {
    return {p.x.toDouble()*100.,p.y.toDouble()*100.,p.z.toDouble()*100.};
}
inline double cutPlane(const CutVertexCm& p,const Entrance& e,unsigned side,unsigned sides) {
    constexpr double pi=3.14159265358979323846;
    // Inner regular polygon: conservative visible polygonal entrance hole.
    const double theta=(2.*pi*(side+0.5))/double(sides);
    const double nx=std::cos(theta),nz=std::sin(theta);
    const double radius=double(e.radiusCm)*std::cos(pi/double(sides));
    return (p.x-double(e.xCm))*nx+(p.z-double(e.zCm))*nz-radius;
}
inline CutVertexCm cutLerp(const CutVertexCm& a,const CutVertexCm& b,double t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
inline std::vector<CutVertexCm> clipCutPolygon(
    const std::vector<CutVertexCm>& poly,const Entrance& e,
    unsigned side,unsigned sides,bool keepOutside) {
    std::vector<CutVertexCm> out;
    if(poly.empty())return out;
    for(size_t i=0;i<poly.size();++i) {
        const auto& a=poly[i];const auto& b=poly[(i+1)%poly.size()];
        const double da=cutPlane(a,e,side,sides),db=cutPlane(b,e,side,sides);
        const bool ia=keepOutside?da>=0:da<=0;
        const bool ib=keepOutside?db>=0:db<=0;
        if(ia)out.push_back(a);
        if(ia!=ib) {
            const double denom=da-db;
            if(denom!=0)out.push_back(cutLerp(a,b,da/denom));
        }
    }
    return out;
}
inline void emitCutPoly(std::vector<CutTriangleCm>& out,
                        const std::vector<CutVertexCm>& poly,size_t limit) {
    if(poly.size()<3)return;
    for(size_t i=1;i+1<poly.size();++i) {
        if(out.size()>=limit)throw std::length_error("Caverns cut triangle budget");
        const auto& a=poly[0];const auto& b=poly[i];const auto& c=poly[i+1];
        const double cross=(b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
        if(std::abs(cross)<1e-8)continue;
        out.push_back({a,b,c});
    }
}
// Clip a single triangle against an inscribed convex hole polygon. At each
// plane, output outside fragments, carry only inside remainder. This is
// polygon DIFFERENCE, not a half-plane intersection.
inline void subtractEntrance(std::vector<CutTriangleCm>& result,
                             const CutTriangleCm& src,const Entrance& e,
                             unsigned sides,size_t limit) {
    std::vector<CutVertexCm> inside{src.a,src.b,src.c};
    for(unsigned side=0;side<sides&&!inside.empty();++side) {
        emitCutPoly(result,clipCutPolygon(inside,e,side,sides,true),limit);
        inside=clipCutPolygon(inside,e,side,sides,false);
    }
}
inline SurfaceRectCm entranceBounds(const Entrance& e) {
    return {e.xCm-e.radiusCm,e.zCm-e.radiusCm,e.xCm+e.radiusCm+1,e.zCm+e.radiusCm+1};
}
inline bool overlaps(SurfaceRectCm a,SurfaceRectCm b) {
    return a.minX<b.maxX && b.minX<a.maxX && a.minZ<b.maxZ && b.minZ<a.maxZ;
}
// Requires authoritative, non-LOD patch with matching original collision and
// render source. Geometry output retains world centimetres (double).
// Up to 16 sides by default, 6..32 allowed; bounded result avoids OOM.
inline CutSurface cutTerrainEntrances(const terraweave::TerrainPatch& patch,
                                     const std::vector<Entrance>& entrances,
                                     unsigned sides=16,size_t maxTriangles=200000) {
    if(patch.presentationOnly||patch.resolution<2||patch.resolution>129||
       patch.cellSizeMetres<=0 || sides<6||sides>32||
       !maxTriangles||maxTriangles>1000000||
       patch.collision.size()!=size_t(patch.resolution-1)*(patch.resolution-1)*2)
        throw std::invalid_argument("Caverns surface cut source/settings");
    const int64_t x=int64_t(patch.cell.x)*patch.cellSizeMetres*100;
    const int64_t z=int64_t(patch.cell.z)*patch.cellSizeMetres*100;
    const SurfaceRectCm tile{x,z,x+int64_t(patch.cellSizeMetres)*100,
                                      z+int64_t(patch.cellSizeMetres)*100};
    CutSurface out;out.cell=patch.cell;out.sourceRevision=patch.sourceRevision;
    std::vector<const Entrance*> candidates;
    for(const auto& e:entrances) {
        if(e.radiusCm<=0||e.surfaceYcm<e.caveYcm)
            throw std::invalid_argument("Caverns invalid entrance");
        const auto bounds=entranceBounds(e);
        if(overlaps(tile,bounds)) {candidates.push_back(&e);out.dirtyRegions.push_back(bounds);}
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){
        return a->id<b->id;
    });
    for(size_t i=1;i<candidates.size();++i)
        if(candidates[i-1]->id==candidates[i]->id)
            throw std::invalid_argument("Caverns duplicate entrance id");
    for(const auto& tri:patch.collision) {
        std::vector<CutTriangleCm> pending{{toCutVertex(tri.a),toCutVertex(tri.b),toCutVertex(tri.c)}};
        for(const Entrance* e:candidates) {
            std::vector<CutTriangleCm> next;
            for(const auto& t:pending) {
                std::vector<CutVertexCm> v{t.a,t.b,t.c};
                const auto bounds=entranceBounds(*e);
                const double minX=std::min({t.a.x,t.b.x,t.c.x}),maxX=std::max({t.a.x,t.b.x,t.c.x});
                const double minZ=std::min({t.a.z,t.b.z,t.c.z}),maxZ=std::max({t.a.z,t.b.z,t.c.z});
                if(maxX<bounds.minX||minX>=bounds.maxX||
                   maxZ<bounds.minZ||minZ>=bounds.maxZ) {
                    if(next.size()>=maxTriangles)throw std::length_error("Caverns cut triangle budget");
                    next.push_back(t);
                }else subtractEntrance(next,t,*e,sides,maxTriangles);
            }
            pending.swap(next);
        }
        if(out.triangles.size()+pending.size()>maxTriangles)
            throw std::length_error("Caverns cut triangle budget");
        out.triangles.insert(out.triangles.end(),pending.begin(),pending.end());
    }
    return out;
}
} // namespace aiverse::terraweave::caverns
