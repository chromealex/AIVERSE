// TerraWeave optional native WorldScene bridge. The actual RHI/physics ground
// is owned by TerrainMutationCoordinator; WorldScene gets one immutable
// snapshot for BOTH nearby forest and distant Horizon visual sampling.
// Native callers must execute this on scene/update thread at a safe barrier.
#pragma once
#include "engine/world/TerraWeaveTerrainRuntime.h"
#include "engine/world/WorldScene.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

class WorldSceneEarthworksBinding {
public:
    uint64_t lastPublishedRevision()const{return publishedRevision_;}
    bool current()const{return published_;}
    size_t replacedTiles()const{return replaced_.size();}

    // Call after each publishVisible() pass. Even while some replacement
    // meshes are pending, immediately hide the old authored render model
    // for EVERY tile that already has a fully published dynamic patch.
    // The immutable near/far forest snapshot is delivered only when all
    // visible/pinned replacement tiles are ready, preventing root/shore
    // height changes from racing ahead of their actual terrain renderer.
    bool synchronize(world::WorldScene& scene,
                     const TerrainMutationCoordinator& terrain) {
        if(scene.worldCellSizeMetres()!=terrain.terrainCellSizeMetres())
            throw std::invalid_argument(
                "TerraWeave WorldScene and runtime tile sizes mismatch");
        const auto cells=terrain.residentCells();
        const bool tileSetChanged=(cells!=replaced_);
        if(tileSetChanged){
            scene.setReplacedTerrainTiles(cells);
            replaced_=cells;
        }
        if(terrain.pendingTiles()!=0)return false;
        const uint64_t revision=terrain.soilRevision();
        if(published_ && revision==publishedRevision_ &&
           !tileSetChanged)
            return true;

        // Source bounds intersect changed published tiles. Reusing the
        // pending dirty queue keeps precise far-HLOD invalidation cheap;
        // if >128 rectangles are present, passing none deliberately
        // triggers the existing whole-region conservative re-bake.
        const auto notifications=terrain.pendingTerrainNotifications();
        std::vector<RectCm> edits;
        if(notifications.size()<=128u){
            edits.reserve(notifications.size());
            for(const auto& change:notifications)
                edits.push_back(change.bounds);
            std::sort(edits.begin(),edits.end(),
                      [](const RectCm& a,const RectCm& b){
                          if(a.minX!=b.minX)return a.minX<b.minX;
                          if(a.minZ!=b.minZ)return a.minZ<b.minZ;
                          if(a.maxX!=b.maxX)return a.maxX<b.maxX;
                          return a.maxZ<b.maxZ;
                      });
            edits.erase(std::unique(edits.begin(),edits.end(),
                       [](const RectCm& a,const RectCm& b){
                           return a.minX==b.minX && a.minZ==b.minZ &&
                                  a.maxX==b.maxX && a.maxZ==b.maxZ;
                       }),edits.end());
        }
        // Copy authoritative Earthworks data once per completed revision,
        // NOT per frame. BaseSampler must capture immutable source storage.
        auto snapshot=terrain.presentationSnapshot();
        scene.setVegetationTerrainSnapshot(std::move(snapshot),
                                           std::move(edits));
        publishedRevision_=revision;
        published_=true;
        return true;
    }

    // Call before WorldScene or native terrain runtime shutdown on the
    // owner thread. The original authored terrain draw and height source
    // will become active again on the next WorldScene update.
    void reset(world::WorldScene& scene){
        scene.setReplacedTerrainTiles({});
        scene.setVegetationTerrainSnapshot(nullptr,{});
        replaced_.clear();
        publishedRevision_=0;
        published_=false;
    }

private:
    std::vector<world::Cell> replaced_;
    uint64_t publishedRevision_=0;
    bool published_=false;
};

} // namespace aiverse::terraweave
