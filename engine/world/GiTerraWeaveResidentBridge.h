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
    // Patches the attach() listener could not convert (bad layout, ids).
    size_t listenerErrors() const { return listenerErrors_; }

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

        uint64_t epoch=0;
        if constexpr(requires{patch.surfaceEpoch;resident.surfaceEpoch();}){
            epoch=patch.surfaceEpoch;
            if(resident.surfaceEpoch()!=epoch)return out;
        }
        const Revision revision{patch.sourceRevision,epoch};
        const auto id=GiWorldGeometryBridge::stableChunkId(
            patch.cell.x,patch.cell.z,GiGeometrySource::TerraWeavePatch);
        auto it=committed_.find(id);
        if (it!=committed_.end() && it->second==revision &&
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
        if (!staged_.contains(id) || staged_.at(id)!=std::optional<Revision>{revision}) {
            GiWorldGeometryBridge::replaceTerraWeavePatch(scene,patch,albedo);
            staged_[id]=revision;
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

    // Retry a staged publication GI refused earlier (pending GPU fence).
    // Call once per frame while !safeToBind().
    template<class GpuRuntime>
    Result retry(render::GiGeometryScene& scene,GpuRuntime& gpu) {
        Result out;
        out.giRevision=scene.revision();
        if(staged_.empty()) {out.alreadyCurrent=true;return out;}
        return attemptPublish(scene,gpu,out);
    }

    // Hook for terraweave::TerrainMutationCoordinator (addPatchListener: other
    // observers such as the Box3D collision publisher stay attached).
    template<class Coordinator,class GpuRuntime>
    void attach(Coordinator& terrain,render::GiGeometryScene& scene,GpuRuntime& gpu,
                render::float3 albedo={.22f,.38f,.16f}) {
        terrain.addPatchListener({
            [this,&scene,&gpu,albedo](const auto& resident,const auto& patch) {
                try {(void)syncPublishedPatch(resident,patch,scene,gpu,albedo);}
                catch(const std::exception&) {++listenerErrors_;}
            },
            [this,&scene,&gpu](world::Cell cell) {
                try {(void)unloadPatch(cell.x,cell.z,scene,gpu);}
                catch(const std::exception&) {++listenerErrors_;}
            }
        });
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
    using Revision=std::pair<uint64_t,uint64_t>; // soil, topology
    std::map<uint64_t,Revision> committed_;  // last GPU-accepted source revisions
    std::map<uint64_t,std::optional<Revision>> staged_; // pending revisions/unloads
    size_t listenerErrors_=0;
};

} // namespace aiverse::world
