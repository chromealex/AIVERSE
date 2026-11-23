#pragma once
// TerraWeave Caverns Stage27: one authoritative CPU cave surface converted
// to BOTH SpatialWorld triangles and Box3D static-mesh input.
// Source vertices are chunk-local METRES; world authority uses integer cm.
// No Scene::create/SpatialWorld::add here. No geometry-based game genres.
#include "engine/world/TerraWeaveCavernsShadowOwners.h"
#include "engine/world/TerraWeaveCavernsMesh.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
namespace aiverse::terraweave::caverns {
struct CaveVolumeCollisionLimits {
    size_t maxTriangles=65534;
    size_t maxVertices=65535;
};
// Native Box3D mesh and SpatialWorld triangles are authored from the same
// integer-cm vertices and indexed winding, including floors and ceilings.
// Box3D mesh vertices are relative to the chunk origin; body position is
// the absolute signed world origin in metres.
inline CaveCollisionPrepared prepareCaveVolumeCollision(
    const CaveMesh& mesh,uint32_t resolution,int32_t spacingCm,
    uint64_t revision,uint32_t userData=0,
    CaveVolumeCollisionLimits limits={}) {
    if(!revision||!resolution||resolution>64||spacingCm<=0||spacingCm>10000||
       limits.maxTriangles==0||limits.maxTriangles>65534||
       limits.maxVertices==0||limits.maxVertices>65535||
       mesh.indices.empty()||mesh.indices.size()%3||
       mesh.indices.size()/3>limits.maxTriangles)
        throw std::invalid_argument("Caverns volume collision source/budget");
    const int64_t span=int64_t(resolution)*spacingCm;
    const int64_t ox=mesh.key.x*span,oy=mesh.key.y*span,oz=mesh.key.z*span;
    // Fixed and Box3D share absolute-world metre limits used by the physics
    // API; reject distant tiles before lossy float conversion.
    if(ox< -10000000||ox>10000000-span||
       oy< -10000000||oy>10000000-span||
       oz< -10000000||oz>10000000-span||
       mesh.key.x<INT32_MIN||mesh.key.x>INT32_MAX||
       mesh.key.y<INT32_MIN||mesh.key.y>INT32_MAX||
       mesh.key.z<INT32_MIN||mesh.key.z>INT32_MAX)
        throw std::out_of_range("Caverns collision physics world bounds");
    CaveCollisionPrepared out;
    out.revision=revision;
    out.cell={int32_t(mesh.key.x),int32_t(mesh.key.y),int32_t(mesh.key.z)};
    auto& body=out.body;
    body.motion=physics::Motion::Static;
    body.shape=physics::ShapeKind::Mesh;
    body.category=physics::kCategoryStatic;
    body.userData=userData;
    body.position={float(double(ox)/100.),float(double(oy)/100.),
                   float(double(oz)/100.)};
    std::map<std::tuple<int64_t,int64_t,int64_t>,int32_t> localVertexIds;
    const auto toLocalCm=[&](MeshVertex v){
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::invalid_argument("Caverns collision nonfinite position");
        const double ext=double(span)/100.;
        if(v.x<0||v.y<0||v.z<0||v.x>ext||v.y>ext||v.z>ext)
            throw std::invalid_argument("Caverns collision outside owning chunk");
        return std::tuple<int64_t,int64_t,int64_t>{
            int64_t(std::llround(double(v.x)*100.)),
            int64_t(std::llround(double(v.y)*100.)),
            int64_t(std::llround(double(v.z)*100.))};
    };
    for(size_t i=0;i<mesh.indices.size();i+=3){
        Vec3 p[3]{};
        int32_t indices[3]{};
        for(int j=0;j<3;++j){
            const uint32_t index=mesh.indices[i+size_t(j)];
            if(index>=mesh.vertices.size())
                throw std::invalid_argument("Caverns collision bad mesh index");
            const auto [x,y,z]=toLocalCm(mesh.vertices[index]);
            p[j]={Fixed::fromRatio(ox+x,100),
                  Fixed::fromRatio(oy+y,100),
                  Fixed::fromRatio(oz+z,100)};
            const auto key=std::make_tuple(x,y,z);
            const auto found=localVertexIds.find(key);
            if(found!=localVertexIds.end())indices[j]=found->second;
            else{
                if(body.vertices.size()>=limits.maxVertices)
                    throw std::length_error("Caverns Box3D mesh vertex budget");
                indices[j]=int32_t(body.vertices.size());
                localVertexIds.emplace(key,indices[j]);
                body.vertices.push_back({float(double(x)/100.),
                                         float(double(y)/100.),
                                         float(double(z)/100.)});
            }
        }
        const world::Triangle tri{p[0],p[1],p[2]};
        if(indices[0]==indices[1]||indices[1]==indices[2]||
           indices[2]==indices[0]||
           !world::SpatialWorld::validTriangle(tri))
            throw std::invalid_argument("Caverns degenerate authoritative triangle");
        out.triangles.push_back(tri);
        body.indices.insert(body.indices.end(),{indices[0],indices[1],indices[2]});
    }
    return out;
}
} // namespace aiverse::terraweave::caverns
