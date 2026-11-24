// Optional native adapter: the same four vertices feed Renderer3D and Box3D.
// Both publishers already own lifetime/fences. No new GPU/physics registry.
#pragma once
#include "GranularWorldIntegration.h"
#include "aiverselib/world/PlanetGranularPhysicalFrame.h"

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
        uint32_t completedTick,GranularFrameBudget budget={},std::string_view localBody={}){
    namespace wp=lib::worldprocess;
    if(!budget.maxResidentTiles||budget.maxResidentTiles>256||
       !budget.maxOccupiedCells||budget.maxOccupiedCells>16384||
       requested.size()>budget.maxResidentTiles||completedTick==UINT32_MAX)
        throw std::invalid_argument("GRANULAR invalid planetary frame budget");
    if(!localBody.empty()&&surface.findBody(std::string(localBody))<0)
        throw std::invalid_argument("GRANULAR unknown local publication body");
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
        const auto location=wp::resolvePlanetGranularPhysicalFrame(ecs,surface,tile.field,p->second.bodyId,
            {p->second.cellSizeCm,p->second.depthCm,p->second.bottomClearanceCm},completedTick,localBody);
        if(!location)throw std::invalid_argument("GRANULAR invalid planetary physical placement or frame");
        const auto& position=location->placement;
        GranularSurfacePatch patch;patch.field=tile.field;patch.firstX=tile.firstX;patch.firstRow=tile.firstRow;
        patch.columns=uint16_t(std::min<unsigned>(16,field->width-tile.firstX));
        patch.rows=uint16_t(std::min<unsigned>(16,field->height-tile.firstRow));
        patch.fieldTick=completedTick;patch.soilRevision=terrainRevision;
        const auto quads=wp::planetGranularTops(ecs,surface,tile.field,position,location->depthCm,
            patch.firstX,patch.firstRow,patch.columns,patch.rows,completedTick);
        if(!quads)throw std::invalid_argument("GRANULAR planetary geometry");
        // Both publishers use the same explicit frame. Mesh vertices are
        // relative to the field origin; subtract anchors in Fixed, not float.
        const auto origin=tr->pos;
        const auto f3=[](Vec3 v){return render::float3{float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};};
        patch.originMetres=f3(location->patchOrigin);
        uint64_t h=14695981039346656037ull;
        const auto mix=[&](uint64_t value){h=granularSignatureMix(h,value);};
        mix(tile.field.packed());mix(uint64_t(location->body));mix(terrainRevision);
        mix(p->second.rgba);mix(tile.firstX);mix(tile.firstRow);mix(patch.columns);mix(patch.rows);
        mix(uint64_t(p->second.cellSizeCm));mix(uint64_t(p->second.depthCm));mix(uint64_t(p->second.bottomClearanceCm));
        for(auto v:{location->publicationOffset.x,location->publicationOffset.y,location->publicationOffset.z})mix(uint64_t(v.raw));
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
// Body-local owner scenes use a single immutable body selection. On a body
// switch or rollback retire/reset both publishers before using another frame.
inline GranularPreparedFrame preparePlanetGranularBodyFrame(
        const World& ecs,const game::Surface& surface,std::string_view bodyId,
        uint64_t terrainRevision,std::span<const GranularResidentTile> requested,
        const std::map<uint64_t,PlanetGranularPlacement>& placements,
        uint32_t completedTick,GranularFrameBudget budget={}){
    if(bodyId.empty())throw std::invalid_argument("GRANULAR body-local frame requires body ID");
    return preparePlanetGranularFrame(ecs,surface,terrainRevision,requested,placements,
        completedTick,budget,bodyId);
}
} // namespace aiverse::examples::world_processes_native
