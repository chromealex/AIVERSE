// Optional native adapter: the same four vertices feed Renderer3D and Box3D.
// Both publishers already own lifetime/fences. No new GPU/physics registry.
#pragma once
#include "GranularWorldIntegration.h"
#include "aiverselib/world/PlanetProcessGeometry.h"

namespace aiverse::examples::world_processes_native {
struct PlanetGranularPlacement {
    std::string bodyId;
    int32_t cellSizeCm=100,depthCm=100,bottomClearanceCm=0;
    uint32_t rgba=0xffaa9966u;
};
inline GranularPreparedFrame preparePlanetGranularFrame(
        const World& ecs,const game::Surface& surface,uint64_t terrainRevision,
        std::span<const GranularResidentTile> requested,
        const std::map<uint64_t,PlanetGranularPlacement>& placements,
        uint32_t completedTick,GranularFrameBudget budget={}){
    namespace wp=lib::worldprocess;
    if(!budget.maxResidentTiles||budget.maxResidentTiles>256||
       !budget.maxOccupiedCells||budget.maxOccupiedCells>16384||
       requested.size()>budget.maxResidentTiles||completedTick==UINT32_MAX)
        throw std::invalid_argument("GRANULAR invalid planetary frame budget");
    GranularPreparedFrame frame;frame.completedTick=completedTick;frame.soilRevision=terrainRevision;
    std::vector<GranularResidentTile> ordered(requested.begin(),requested.end());
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){
        return std::tuple{a.field.packed(),a.firstRow,a.firstX}<
               std::tuple{b.field.packed(),b.firstRow,b.firstX};
    });
    for(const auto& tile:ordered){
        const auto* field=ecs.tryGetConst<granular::Field>(tile.field);
        const auto* tr=ecs.tryGetConst<Transform>(tile.field);
        const auto p=placements.find(tile.field.packed());
        if(!field||!tr||p==placements.end()||tile.firstX%16||tile.firstRow%16||
           tile.firstX>=field->width||tile.firstRow>=field->height)
            throw std::invalid_argument("GRANULAR planetary tile placement");
        const auto location=wp::surfaceFrame(&surface,tr->pos);
        if(!location||surface.bodies[size_t(location->body)].id!=p->second.bodyId)
            throw std::invalid_argument("GRANULAR planetary body mismatch");
        const granular::SurfaceFieldPlacement position{tr->pos,p->second.cellSizeCm,p->second.bottomClearanceCm};
        GranularSurfacePatch patch;patch.field=tile.field;patch.firstX=tile.firstX;patch.firstRow=tile.firstRow;
        patch.columns=uint16_t(std::min<unsigned>(16,field->width-tile.firstX));
        patch.rows=uint16_t(std::min<unsigned>(16,field->height-tile.firstRow));
        patch.fieldTick=completedTick;patch.soilRevision=terrainRevision;
        const auto quads=wp::planetGranularTops(ecs,surface,tile.field,position,p->second.depthCm,
            patch.firstX,patch.firstRow,patch.columns,patch.rows,completedTick);
        if(!quads)throw std::invalid_argument("GRANULAR planetary geometry");
        // Existing publishers use the same simulation-space origin. Planet
        // orbit/camera-relative render hosts must rebase in their owner adapter.
        const auto origin=tr->pos;
        const auto f3=[](Vec3 v){return render::float3{float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};};
        patch.originMetres=f3(origin);
        uint64_t h=14695981039346656037ull;
        const auto mix=[&](uint64_t value){h=granularSignatureMix(h,value);};
        mix(tile.field.packed());mix(uint64_t(location->body));mix(terrainRevision);
        mix(p->second.rgba);mix(tile.firstX);mix(tile.firstRow);mix(patch.columns);mix(patch.rows);
        for(auto v:{origin.x,origin.y,origin.z})mix(uint64_t(v.raw));
        for(const auto& q:*quads){
            const auto n=int32_t(patch.collisionVertices.size());
            mix(q.column);mix(q.row);
            for(auto v:{q.normal.x,q.normal.y,q.normal.z})mix(uint64_t(v.raw));
            for(const auto& point:q.vertices){
                for(auto v:{point.x,point.y,point.z})mix(uint64_t(v.raw));
                const auto local=f3(point-origin);
                patch.visual.addVertex(local,f3(q.normal),p->second.rgba);
                patch.collisionVertices.push_back({local.x,local.y,local.z});
            }
            patch.visual.addTriangle(uint32_t(n),uint32_t(n+1),uint32_t(n+2));
            patch.visual.addTriangle(uint32_t(n),uint32_t(n+2),uint32_t(n+3));
            patch.collisionIndices.insert(patch.collisionIndices.end(),{n,n+1,n+2,n,n+2,n+3});
        }
        patch.activeCells=quads->size();patch.geometrySignature=h;
        frame.occupiedCells+=patch.activeCells;
        if(frame.occupiedCells>budget.maxOccupiedCells||!frame.keys.insert(granularPatchKey(patch)).second)
            throw std::invalid_argument("GRANULAR planetary frame duplicate/budget");
        frame.patches.push_back(std::move(patch));
    }
    return frame;
}
} // namespace aiverse::examples::world_processes_native
