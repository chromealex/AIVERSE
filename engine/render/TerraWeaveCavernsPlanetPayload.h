#pragma once
// Prepare-only conversion; existing GeometryScene and collision owners must
// use this payload, rather than publishing the chart mesh in global XYZ.
#include "engine/world/TerraWeaveCavernsPlanetSurface.h"
#include "engine/render/TerraWeaveCavernsRimPayload.h"
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetNativePayload {
    CavePlanetAnchor anchor{};
    int body=-1;
    Vec3 simulationOrigin{};
    CaveCollisionPrepared collision;
    std::unique_ptr<render::Model> model;
    // Same local model coordinates as Box3D vertices, different origin
    // only: simulation anchor versus actual orbit centre at render time.
    Vec3 renderOrigin(const game::Surface& surface,Fixed seconds)const{
        if(body<0||size_t(body)>=surface.bodies.size()||
           surface.bodies[size_t(body)].id!=anchor.bodyId)
            throw std::logic_error("Caverns planetary render body identity");
        return surface.bodyPosition(body,seconds)+simulationOrigin-game::Surface::anchor(body);
    }
};
inline CavePlanetNativePayload prepareCavePlanetNativeTriangles(
        const CavePlanetFrame& frame,std::span<const world::Triangle> chartTriangles,
        world::Cell ownerCell,uint64_t nativeRevision,Vec3 chartOrigin={},uint32_t userData=0){
    if(!nativeRevision||chartTriangles.size()>65534)
        throw std::invalid_argument("Caverns planetary native triangle budget");
    CavePlanetNativePayload out;
    out.anchor=frame.anchor();out.body=frame.body();
    auto centre=frame.toSimulation(chartOrigin);
    // Integer origin avoids float precision loss at the simulation anchor.
    out.simulationOrigin={Fixed::fromInt(centre.x.floorInt()),Fixed::fromInt(centre.y.floorInt()),Fixed::fromInt(centre.z.floorInt())};
    out.collision.cell=ownerCell;out.collision.revision=nativeRevision;
    out.collision.triangles.reserve(chartTriangles.size());
    for(const auto& t:chartTriangles){
        world::Triangle transformed{frame.toSimulation(t.a),frame.toSimulation(t.b),frame.toSimulation(t.c)};
        if(!world::SpatialWorld::validTriangle(transformed))
            throw std::invalid_argument("Caverns planetary native triangle lost precision");
        out.collision.triangles.push_back(transformed);
    }
    out.model=buildExactCavePayload(out.collision,out.simulationOrigin,userData);
    return out;
}
inline CavePlanetNativePayload prepareCavePlanetNativeChunk(
        const CavePlanetFrame& frame,const CaveMesh& mesh,uint32_t resolution,
        int32_t spacingCm,uint64_t nativeRevision,uint32_t userData=0){
    auto local=prepareCaveVolumeCollision(mesh,resolution,spacingCm,nativeRevision,userData);
    const int64_t span=int64_t(resolution)*spacingCm;
    const Vec3 origin{Fixed::fromRatio(mesh.key.x*span,100),Fixed::fromRatio(mesh.key.y*span,100),Fixed::fromRatio(mesh.key.z*span,100)};
    return prepareCavePlanetNativeTriangles(frame,local.triangles,local.cell,nativeRevision,origin,userData);
}
} // namespace aiverse::terraweave::caverns
