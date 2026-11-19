// One-call opt-in native game HOST presentation barrier for TerraWeave.
// Consumes a canonical Simulation World snapshot. Does NOT install itself
// into RTS/FPS, create an ECS World, step Box3D or alter NEREID water bodies.
// The caller still owns the gfx::Device, SpatialWorld and nav::Mesh lifetimes.
#pragma once
#include "engine/world/TerraWeaveNativeWorldBinding.h"
#include "engine/world/TerraWeaveNavMeshBridge.h"
#include "engine/world/NereidFloodVisualScene.h"
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::terraweave {

struct NativeTerrainPublishResult {
    uint64_t ecsStateRevision=0;
    uint64_t groundRevision=0;
    size_t terrainUploads=0;
    size_t grassUploads=0;
    size_t remainingTiles=0;
    bool ecsStateChanged=false;
    bool groundReady=false;
    bool forestReady=false;
    bool navigationReady=false;
};

// Native application update phase only: no calls from the deterministic
// ECS systems or asynchronous prepare jobs. Pinned tile cells MUST cover
// all potential gameplay collision queries, not merely the view frustum.
// The host reserves collision ID ranges per tile, distinct from actors,
// and removes the old authored static tile colliders when opting in.
inline NativeTerrainPublishResult publishNativeTerraWeaveFrame(
        const World& ecs,NativeTerraWeaveWorldBinding& binding,
        TerrainMutationCoordinator& terrain,
        world::WorldScene& worldScene,gfx::Device& device,
        world::SpatialWorld& spatial,nav::Mesh& navmesh,
        const EarthworksNavSource& navSource,
        std::span<const world::Cell> pinnedCells,
        RectCm grassArea,
        const TerrainMutationCoordinator::ColliderIdForCell& ids,
        std::string* navError=nullptr,
        size_t terrainUploadBudget=2u,bool forceRehydrate=false) {
    if(!ids)throw std::invalid_argument(
        "TerraWeave native host needs collision ID owner");

    NativeTerrainPublishResult status;
    const auto* resource=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!resource)
        throw std::logic_error("TerraWeave native ECS resource missing");
    status.ecsStateRevision=resource->stateRevision;
    // Only actual floodwater beyond the authored NEREID surface is
    // converted into one bounded visual mesh. Native water ECS remains the
    // authoritative volume source and the scene owns only copied vertices.
    if(const auto* flood=ecs.resourceConst<
            lib::nereid::WaterFloodResource>()){
        worldScene.setNereidFloodCells(
            world::collectNereidFloodVisualCells(
                ecs,*flood,grassArea.minX,grassArea.minZ,
                grassArea.maxX,grassArea.maxZ));
    }
    status.ecsStateChanged=binding.reconcile(
        ecs,worldScene,forceRehydrate);

    status.terrainUploads=terrain.publishVisible(
        device,spatial,pinnedCells,terrainUploadBudget,ids);
    status.remainingTiles=terrain.pendingTiles();
    status.groundReady=binding.groundReady();
    status.forestReady=binding.publishScene(worldScene);
    status.groundRevision=terrain.soilRevision();

    // Navigation and grass are only advanced once collision and immutable
    // forest ground are coherent, avoiding gameplay reading one revision
    // while renderer/physics is still showing a previous tile.
    if(status.groundReady) {
        status.navigationReady=terrain.refreshNavMesh(
            navmesh,navSource,navError);
        if(status.navigationReady)
            status.grassUploads=terrain.publishGrass(
                device,grassArea);
    }
    return status;
}

} // namespace aiverse::terraweave
