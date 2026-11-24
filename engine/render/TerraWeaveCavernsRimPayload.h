#pragma once
// CPU payload only. Existing native owners must replace the isolated
// original shaft section as one transaction, not draw/insert both.
#include "engine/world/TerraWeaveCavernsRimTransition.h"
#include "engine/world/TerraWeaveCavernsShadowOwners.h"
#include "engine/render/TerraWeaveCavernsGeometryScene.h"
#include <map>
#include <memory>

namespace aiverse::terraweave::caverns {
struct CaveRimReplacement {
    uint64_t entranceId=0,surfaceEpoch=0,authorityRevision=0;
    CaveRimTransition collar;
    CaveCollisionPrepared collision;
    std::unique_ptr<render::Model> model;
};
inline CaveRimReplacement prepareCaveRimReplacement(
        const CaveSurfaceRim& rim,const Entrance& entrance,
        std::span<const world::Triangle> isolatedSource,Fixed plane,
        uint64_t surfaceEpoch,uint64_t authorityRevision,uint64_t nativeRevision,
        world::Cell ownerCell,Vec3 origin,uint32_t userData=0){
    if(!nativeRevision)throw std::invalid_argument("Caverns rim native revision");
    auto retained=trimOwnedSdfShaft(isolatedSource,plane);
    auto collar=buildCaveRimTransition(rim,entrance,retained,plane,surfaceEpoch,authorityRevision);
    if(retained.size()+collar.triangles.size()>65534)
        throw std::length_error("Caverns rim replacement triangle budget");
    CaveRimReplacement out;
    out.entranceId=entrance.id;out.surfaceEpoch=surfaceEpoch;out.authorityRevision=authorityRevision;
    out.collision.revision=nativeRevision;out.collision.cell=ownerCell;
    out.collision.triangles=std::move(retained);
    out.collision.triangles.insert(out.collision.triangles.end(),collar.triangles.begin(),collar.triangles.end());
    out.collar=std::move(collar);
    auto& body=out.collision.body;
    body.motion=physics::Motion::Static;body.shape=physics::ShapeKind::Mesh;
    body.category=physics::kCategoryStatic;body.userData=userData;
    for(const auto p:{origin.x,origin.y,origin.z})if(std::abs(p.toDouble())>100000.)
        throw std::out_of_range("Caverns rim physics origin");
    body.position={float(origin.x.toDouble()),float(origin.y.toDouble()),float(origin.z.toDouble())};
    std::map<SurfaceVertexKey,int32_t> indices;
    CaveMesh mesh;
    for(const auto& tri:out.collision.triangles){
        const Vec3 points[]={tri.a-origin,tri.b-origin,tri.c-origin};
        render::float3 p[3];
        for(unsigned i=0;i<3;++i){
            const auto v=points[i];
            p[i]={float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};
            // Bound the float local conversion; reject if native Q16
            // identity cannot be recovered from the local coordinates.
            if(std::llround(double(p[i].x)*Fixed::kOne)!=v.x.raw||
               std::llround(double(p[i].y)*Fixed::kOne)!=v.y.raw||
               std::llround(double(p[i].z)*Fixed::kOne)!=v.z.raw)
                throw std::invalid_argument("Caverns rim local float precision");
        }
        const auto cross=render::cross(p[1]-p[0],p[2]-p[0]);
        if(render::dot(cross,cross)<=0.f)throw std::invalid_argument("Caverns rim float degenerate");
        const auto n=render::normalize(cross);
        for(unsigned i=0;i<3;++i){
            const auto key=surfaceVertexKey(points[i]);
            auto it=indices.find(key);
            if(it==indices.end()){
                if(body.vertices.size()>=65535)throw std::length_error("Caverns rim vertex budget");
                it=indices.emplace(key,int32_t(body.vertices.size())).first;
                body.vertices.push_back({p[i].x,p[i].y,p[i].z});
            }
            body.indices.push_back(it->second);
            mesh.indices.push_back(uint32_t(mesh.vertices.size()));
            mesh.vertices.push_back({p[i].x,p[i].y,p[i].z,n.x,n.y,n.z});
        }
    }
    out.model=makeCaveGeometryModel(mesh);
    return out;
}
} // namespace aiverse::terraweave::caverns
