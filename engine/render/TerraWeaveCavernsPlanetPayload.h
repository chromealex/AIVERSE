#pragma once
// Prepare-only conversion; existing GeometryScene and collision owners must
// use this payload, rather than publishing the chart mesh in global XYZ.
#include "engine/world/TerraWeaveCavernsPlanetCut.h"
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
// Preserve the cut planner's actual body-space boundary vertices. Never
// round-trip original planet terrain through the approximate chart inverse.
inline CavePlanetNativePayload prepareCavePlanetCutSlotPayload(
        const CavePlanetFrame& frame,const CavePlanetCutPlan& plan,
        CavePlanetSurfaceSlotKey key,world::Cell ownerCell,uint64_t nativeRevision,
        uint32_t userData=0){
    if(!nativeRevision||!sameCavePlanetAnchor(frame.anchor(),plan.anchor)||
       !plan.surfaceRevision||!plan.surfaceEpoch||!plan.authorityRevision)
        throw std::invalid_argument("Caverns planetary cut payload identity");
    const auto& slot=plan.slots.at(key);
    if(slot.bodyTriangles.size()!=slot.chartTriangles.size()||slot.bodyTriangles.size()>65534)
        throw std::invalid_argument("Caverns planetary cut payload topology");
    CavePlanetNativePayload out;out.anchor=frame.anchor();out.body=frame.body();
    const auto anchor=game::Surface::anchor(frame.body());
    const auto centre=frame.toSimulation({});
    out.simulationOrigin={Fixed::fromInt(centre.x.floorInt()),Fixed::fromInt(centre.y.floorInt()),Fixed::fromInt(centre.z.floorInt())};
    out.collision.cell=ownerCell;out.collision.revision=nativeRevision;
    out.collision.triangles.reserve(slot.bodyTriangles.size());
    for(const auto& t:slot.bodyTriangles){
        world::Triangle transformed{t.a+anchor,t.b+anchor,t.c+anchor};
        if(!world::SpatialWorld::validTriangle(transformed))
            throw std::invalid_argument("Caverns planetary cut collision triangle");
        out.collision.triangles.push_back(transformed);
    }
    out.model=buildExactCavePayload(out.collision,out.simulationOrigin,userData);
    return out;
}
// Check the ACTUAL transformed final volume source against the terrain
// boundary, not merely chart-space equality or a matching revision.
inline bool planetRimNativeBoundaryMatches(const CavePlanetFrame& frame,
        const CaveSurfaceRim& rim,std::span<const world::Triangle> finalVolume){
    if(rim.ring.size()<3||finalVolume.size()>262144)return false;
    std::set<SurfaceEdgeKey> expected;
    const auto point=[&](const SurfaceVertexKey& key){
        return frame.toSimulation({Fixed::fromRaw(key[0]),Fixed::fromRaw(key[1]),Fixed::fromRaw(key[2])});
    };
    for(size_t i=0;i<rim.ring.size();++i)
        if(!expected.insert(surfaceEdgeKey(point(rim.ring[i]),point(rim.ring[(i+1)%rim.ring.size()]))).second)return false;
    std::map<SurfaceEdgeKey,unsigned> uses;
    for(const auto& t:finalVolume){
        if(!world::SpatialWorld::validTriangle(t))return false;
        const Vec3 p[]={t.a,t.b,t.c};
        for(unsigned i=0;i<3;++i){auto edge=surfaceEdgeKey(p[i],p[(i+1)%3]);
            if(expected.contains(edge)&&++uses[edge]>1)return false;}
    }
    return uses.size()==expected.size();
}
// Constrained chart-radial shaft replacement. The caller must actually
// own and replace the isolated source section, never overlay this result
// on a still-resident original volume. Both terrain and collar use the
// same chart and native simulation anchor.
inline CavePlanetNativePayload prepareCavePlanetRimPayload(
        const CavePlanetFrame& frame,const CavePlanetCutPlan& plan,
        const CaveVolumeSnapshot& snapshot,uint64_t entranceId,
        std::span<const world::Triangle> isolatedChartSource,Fixed lowerPlane,
        world::Cell ownerCell,uint64_t nativeRevision,uint32_t userData=0){
    if(!sameCavePlanetAnchor(frame.anchor(),plan.anchor)||
       plan.authorityRevision!=snapshot.sourceRevision||!plan.surfaceEpoch)
        throw std::invalid_argument("Caverns planetary collar plan identity");
    validateCavePlanetVolume(frame,snapshot);
    const auto e=std::find_if(snapshot.entrances.begin(),snapshot.entrances.end(),
        [&](const auto& value){return value.id==entranceId;});
    const auto rim=std::find_if(plan.rims.begin(),plan.rims.end(),
        [&](const auto& value){return value.entranceId==entranceId;});
    if(e==snapshot.entrances.end()||rim==plan.rims.end()||
       lowerPlane<Fixed::fromRatio(e->caveYcm,100)||lowerPlane>=Fixed::fromRatio(e->surfaceYcm,100))
        throw std::invalid_argument("Caverns planetary collar entrance or plane");
    const auto cx=double(e->xCm)/100.,cz=double(e->zCm)/100.,radius=double(e->radiusCm)/100.;
    for(const auto& t:isolatedChartSource){
        const bool trim=t.a.y>lowerPlane||t.b.y>lowerPlane||t.c.y>lowerPlane;
        for(const auto& p:{t.a,t.b,t.c}){
            frame.checkLocal(p);
            if(trim&&std::hypot(p.x.toDouble()-cx,p.z.toDouble()-cz)>radius*1.5)
                throw std::invalid_argument("Caverns planetary source section not isolated");
        }
    }
    auto retained=trimOwnedSdfShaft(isolatedChartSource,lowerPlane);
    const auto collar=buildCaveRimTransition(*rim,*e,retained,lowerPlane,plan.surfaceEpoch,plan.authorityRevision);
    if(collar.triangles.size()>65534-retained.size())
        throw std::length_error("Caverns planetary collar triangle budget");
    retained.insert(retained.end(),collar.triangles.begin(),collar.triangles.end());
    auto result=prepareCavePlanetNativeTriangles(frame,retained,ownerCell,nativeRevision,
        CavePlanetFrame::metres({e->xCm,e->caveYcm,e->zCm}),userData);
    if(!planetRimNativeBoundaryMatches(frame,*rim,result.collision.triangles))
        throw std::invalid_argument("Caverns planetary native rim mismatch");
    return result;
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
