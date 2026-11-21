// Explicit read-barrier consumer for TerraWeave::TerrainPatchResident.
// The real TerraWeave headers are intentionally NOT imported from its actively
// branch; this template uses its published API exactly (engine/render/TerraWeaveTerrainPatchRender.h):
//   resident.resident(), resident.cell(), resident.revision(),
//   resident.pendingDirty() -> TerrainPatchDirtyRegion{cell,sourceRevision,bounds}
//   patch.cell, patch.sourceRevision, patch.collision, patch.resolution, ...
// Called by a native host ONLY AFTER TerrainPatchResident::publish succeeded.
// The GI bridge does not consume takeDirty(), which belongs to other producers.
#pragma once
#include "engine/world/GiWorldGeometryBridge.h"
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace aiverse::world {

class GiTerraWeaveResidentBridge {
public:
    struct Result {
        bool hasNotification=false, staged=false, published=false;
        bool blocked=false, alreadyCurrent=false;
        uint64_t sourceRevision=0, giRevision=0;
        size_t changedRegions=0;
    };

    size_t committedCount() const { return committed_.size(); }
    size_t stagedCount() const { return staged_.size(); }
    bool safeToBind() const { return staged_.empty(); }

    // If the terrain mesh/collision already changed but its new GI snapshot
    // cannot yet be published (outstanding GPU fence), the caller MUST suppress
    // dynamic GI presentation until this method succeeds on a later frame.
    template<class Renderer>
    void guardRenderer(Renderer& renderer) const {
        if (!safeToBind()) renderer.clearDynamicGiOverlay();
    }

    // GpuRuntime: GiMainGpuBridge or GiMainCpuBridge (GiGeometryScene::publish).
    template<class Resident,class Patch,class GpuRuntime>
    Result syncPublishedPatch(const Resident& resident,const Patch& patch,
                              render::GiGeometryScene& scene,GpuRuntime& gpu,
                              render::float3 albedo={.22f,.38f,.16f}) {
        Result out;
        out.sourceRevision=patch.sourceRevision;
        out.giRevision=scene.revision();
        if (!resident.resident() || resident.revision()!=patch.sourceRevision ||
            !(resident.cell()==patch.cell))
            return out; // never publish a pre-commit worker patch into GI

        const auto id=GiWorldGeometryBridge::stableChunkId(
            patch.cell.x,patch.cell.z,GiGeometrySource::TerraWeavePatch);
        auto it=committed_.find(id);
        if (it!=committed_.end() && it->second==patch.sourceRevision &&
            !staged_.contains(id)) {
            out.alreadyCurrent=true;
            return out;
        }
        const auto& dirty=resident.pendingDirty();
        for(const auto& event:dirty) {
            if(event.cell==patch.cell && event.sourceRevision==patch.sourceRevision) {
                out.hasNotification=true;
                break;
            }
        }
        // A retry is allowed even if another consumer already cleared
        // notifications after the GI geometry was staged.
        if (!out.hasNotification && !staged_.contains(id)) return out;
        if (!staged_.contains(id) || staged_.at(id)!=std::optional<uint64_t>{patch.sourceRevision}) {
            GiWorldGeometryBridge::replaceTerraWeavePatch(scene,patch,albedo);
            staged_[id]=patch.sourceRevision;
            out.staged=true;
        }
        return attemptPublish(scene,gpu,out);
    }

    template<class GpuRuntime>
    Result unloadPatch(int64_t cellX,int64_t cellZ,
                       render::GiGeometryScene& scene,GpuRuntime& gpu) {
        Result out;
        out.giRevision=scene.revision();
        const auto id=GiWorldGeometryBridge::stableChunkId(
            cellX,cellZ,GiGeometrySource::TerraWeavePatch);
        if (!committed_.contains(id) && !staged_.contains(id)) {
            out.alreadyCurrent=true;
            return out;
        }
        if(!staged_.contains(id) || staged_.at(id).has_value()) {
            scene.removeChunk(id);
            staged_[id]=std::nullopt; // explicit unload sentinel
            out.staged=true;
        }
        return attemptPublish(scene,gpu,out);
    }

private:
    template<class GpuRuntime>
    Result attemptPublish(render::GiGeometryScene& scene,GpuRuntime& gpu,Result out) {
        const auto stats=scene.publish(gpu);
        out.published=stats.published;
        out.blocked=!stats.published && scene.hasPendingChanges();
        out.giRevision=stats.revision;
        out.changedRegions=stats.changedRegions;
        if(stats.published) {
            for(const auto& [id,revision]:staged_) {
                if(revision)committed_[id]=*revision;
                else committed_.erase(id);
            }
            staged_.clear();
        }
        return out;
    }
    std::map<uint64_t,uint64_t> committed_;  // last GPU-accepted source revisions
    std::map<uint64_t,std::optional<uint64_t>> staged_; // pending revisions/unloads
};

} // namespace aiverse::world
