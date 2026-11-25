// Host barrier that translates the SAME immutable GRANULAR ECS+TerraWeave
// snapshot into physics and rendering. Clients choose the active tiles;
// neither publisher can read or modify authoritative gameplay World.
#pragma once
#include "GranularTerraWeaveBinding.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::examples::world_processes_native {

struct GranularResidentTile {
    Entity field=kNullEntity;
    std::uint16_t firstX=0,firstRow=0; // multiples of 16
};
struct GranularPreparedFrame {
    std::uint32_t completedTick=0;
    std::uint64_t soilRevision=0;
    std::vector<GranularSurfacePatch> patches;
    std::set<GranularPatchKey> keys;
    std::size_t occupiedCells=0;
};
struct GranularFrameBudget {
    std::size_t maxResidentTiles=256;
    std::size_t maxOccupiedCells=16384;
};

// The caller must include both physics-relevant tiles and visible tiles.
// Never cull a ground collider underneath a live Box3D actor solely because
// a camera can no longer see it. The fixed-step World is read-only here.
inline GranularPreparedFrame prepareGranularFrame(
        const World& ecs,const terraweave::EarthworksLayer& terrain,
        std::span<const GranularResidentTile> requested,
        const std::map<std::uint64_t,GranularSurfaceSettings>& placements,
        std::uint32_t completedTick,GranularFrameBudget budget={}) {
    if(budget.maxResidentTiles==0||budget.maxResidentTiles>256||
       budget.maxOccupiedCells==0||budget.maxOccupiedCells>16384||
       requested.size()>budget.maxResidentTiles)
        throw std::invalid_argument("GRANULAR invalid resident tile budget");
    GranularPreparedFrame out;
    out.completedTick=completedTick;
    out.soilRevision=terrain.revision();
    std::vector<GranularResidentTile> ordered(requested.begin(),requested.end());
    std::sort(ordered.begin(),ordered.end(),
        [](const auto& a,const auto& b){
            if(a.field.packed()!=b.field.packed())
                return a.field.packed()<b.field.packed();
            if(a.firstRow!=b.firstRow)return a.firstRow<b.firstRow;
            return a.firstX<b.firstX;
        });
    out.patches.reserve(ordered.size());
    for(const auto& selection:ordered){
        if(!ecs.alive(selection.field)||selection.firstX%16||
           selection.firstRow%16)
            throw std::invalid_argument("GRANULAR invalid tile anchor");
        const auto* field=ecs.tryGetConst<granular::Field>(selection.field);
        if(!field||!granular::valid(*field)||!field->enabled||
           field->sampledTick!=completedTick ||
           selection.firstX>=field->width||
           selection.firstRow>=field->height)
            throw std::invalid_argument("GRANULAR requested unready or stale ECS field");
        const auto position=placements.find(selection.field.packed());
        if(position==placements.end()||!valid(position->second))
            throw std::invalid_argument("GRANULAR missing valid world placement");
        const auto cols=std::uint16_t(std::min<unsigned>(
            16,unsigned(field->width-selection.firstX)));
        const auto rows=std::uint16_t(std::min<unsigned>(
            16,unsigned(field->height-selection.firstRow)));
        auto patch=bakeGranularSurface(ecs,selection.field,terrain,
                                      selection.firstX,selection.firstRow,
                                      cols,rows,position->second);
        const auto key=granularPatchKey(patch);
        if(!out.keys.insert(key).second)
            throw std::invalid_argument("GRANULAR duplicate tile in frame");
        out.occupiedCells+=patch.activeCells;
        if(out.occupiedCells>budget.maxOccupiedCells)
            throw std::length_error("GRANULAR frame occupied cell budget");
        out.patches.push_back(std::move(patch));
    }
    return out;
}

// Box3D owner thread, BETWEEN Scene::step calls. Caller must not step
// physics if publication returns false: some newer tiles might be resident
// while others retain the old surface after a rejected allocation.
inline bool publishGranularPhysicsFrame(
        physics::Scene& scene,GranularBox3DPublisher& publisher,
        const GranularPreparedFrame& frame,std::uint32_t userData=0){
    for(const auto& tile:frame.patches)
        if(!publisher.publish(scene,tile,userData))return false;
    publisher.retireAbsent(scene,frame.keys);
    return true;
}

// Graphics owner thread, outside Renderer3D frame encoding. Same immutable
// frame data, same topology/height samples as the Box3D static surface.
inline bool publishGranularRenderFrame(
        gfx::Device& device,GranularRenderPublisher& publisher,
        const GranularPreparedFrame& frame){
    for(const auto& tile:frame.patches)
        if(!publisher.publish(device,tile))return false;
    return publisher.retireAbsent(device,frame.keys);
}

} // namespace aiverse::examples::world_processes_native
