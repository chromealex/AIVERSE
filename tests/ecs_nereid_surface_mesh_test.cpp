// NEREID stage 7B -- presentation-only mesh topology and LOD.
#include "engine/world/nereid/NereidWaterSurfaceMesh.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
namespace n=aiverse::lib::nereid;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static double signedUpArea(const n::WaterSurfaceMesh& mesh) {
    double area=0;
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        area+=((b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z))*0.5;
    }
    return area;
}
static void checkMesh(const n::WaterSurfaceMesh& m,const char* why) {
    check(!m.empty() && m.indices.size()%3==0,why);
    for(const auto& v:m.vertices)
        check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
              std::isfinite(v.u)&&std::isfinite(v.v)&&std::isfinite(v.shore)&&
              v.shore>=0&&v.shore<=1,"invalid presentation vertex");
    for(auto i:m.indices)check(i<m.vertices.size(),"mesh index out of range");
    check(signedUpArea(m)>0,"triangles not facing +Y");
}
int main() {
    n::WaterBodyComponent b;
    b.surfaceY=12_fx;b.bottomY=-2_fx;
    b.footprint=n::WaterFootprint::Box;
    b.center={10_fx,0_fx,20_fx};
    b.halfExtents={5_fx,7_fx};
    n::WaterSurfaceMeshOptions opts;opts.lod=n::WaterSurfaceLod::Far;
    const auto box=n::buildWaterSurfaceMesh(b,nullptr,opts);
    checkMesh(box,"box mesh missing");
    for(const auto& vertex:box.vertices)
        check(vertex.shore==0,"far LOD should not tint whole water white");
    check(box.triangleCount()==2 && std::abs(signedUpArea(box)-140.0)<1e-4,
          "box area/winding");
    for(const auto& vertex:box.vertices)check(vertex.y==12,"surface elevation lost");
    b.footprint=n::WaterFootprint::Disc;b.radius=6_fx;
    const auto disc=n::buildWaterSurfaceMesh(b,nullptr,opts);
    checkMesh(disc,"disc mesh missing");
    check(disc.triangleCount()==12,"far disc tessellation");
    opts.lod=n::WaterSurfaceLod::Near;
    const auto near=n::buildWaterSurfaceMesh(b,nullptr,opts);
    checkMesh(near,"near disc mesh missing");
    check(near.triangleCount()>disc.triangleCount(),"LOD did not simplify disc");
    const auto tooSmall=[&]{
        auto limited=opts;limited.maxVertices=4;
        return n::buildWaterSurfaceMesh(b,nullptr,limited);
    }();
    check(tooSmall.empty(),"mesh vertex cap ignored");

    n::WaterGeometryComponent polygon;
    polygon.kind=n::WaterGeometryKind::Polygon;
    polygon.points={{0_fx,0_fx,0_fx},{6_fx,0_fx,0_fx},
                    {6_fx,6_fx,0_fx},{3_fx,2_fx,0_fx},
                    {0_fx,6_fx,0_fx}}; // concave
    opts.lod=n::WaterSurfaceLod::Far;
    const auto lake=n::buildWaterSurfaceMesh(b,&polygon,opts);
    checkMesh(lake,"concave polygon triangulation");
    check(lake.triangleCount()==3,"polygon triangulation count");
    const double area=signedUpArea(lake);
    std::reverse(polygon.points.begin(),polygon.points.end());
    const auto reversed=n::buildWaterSurfaceMesh(b,&polygon,opts);
    checkMesh(reversed,"reversed polygon winding");
    check(std::abs(signedUpArea(reversed)-area)<1e-4,"reversed polygon area");
    polygon.points={{0_fx,0_fx,0_fx},{6_fx,6_fx,0_fx},
                    {0_fx,6_fx,0_fx},{6_fx,0_fx,0_fx}};
    check(n::buildWaterSurfaceMesh(b,&polygon,opts).empty(),
          "self-intersecting polygon accepted as render surface");

    n::WaterGeometryComponent river;
    river.kind=n::WaterGeometryKind::RiverSpline;
    river.points={{0_fx,0_fx,1_fx},{10_fx,0_fx,2_fx},
                  {15_fx,6_fx,1_fx}};
    const auto riverMesh=n::buildWaterSurfaceMesh(b,&river,opts);
    checkMesh(riverMesh,"river strip");
    check(riverMesh.vertices.size()==6 && riverMesh.triangleCount()==4,
          "river strip topology");
    // Far LOD intentionally disables vertex shoreline foam: every
    // far-river vertex is on its perimeter, so tinting would turn it white.
    for(const auto& v:riverMesh.vertices)
        check(v.shore==0.f,"far river must not tint the entire surface");
    auto foamOpts=opts;
    foamOpts.lod=n::WaterSurfaceLod::Medium;
    const auto foamyRiver=n::buildWaterSurfaceMesh(b,&river,foamOpts);
    checkMesh(foamyRiver,"medium river mesh missing");
    bool foundShore=false;
    for(const auto& v:foamyRiver.vertices)foundShore|=v.shore>0.0f;
    check(foundShore,"medium river shoreline factor missing");
    b.footprint=n::WaterFootprint::Infinite;
    opts.cameraX=100;opts.cameraZ=200;opts.oceanPatchRadius=50;
    const auto ocean=n::buildWaterSurfaceMesh(b,nullptr,opts);
    checkMesh(ocean,"camera-relative ocean patch");
    check(ocean.infinite && ocean.triangleCount()==2,"ocean patch size");
    check(std::abs(signedUpArea(ocean)-10000.0)<1e-4,"ocean patch area");
    std::cout<<"PASS NEREID presentation geometry, LOD, polygon, river, ocean and winding\n";
}
