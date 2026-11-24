// One-call opt-in native game HOST presentation barrier for TerraWeave.
// Consumes a canonical Simulation World snapshot. Does NOT install itself
// into RTS/FPS, create an ECS World, step Box3D or alter NEREID water bodies.
// The caller still owns the gfx::Device, SpatialWorld and nav::Mesh lifetimes.
#pragma once
#include "engine/world/TerraWeaveNativeWorldBinding.h"
#include "engine/world/TerraWeaveNavMeshBridge.h"
#include "engine/world/TerraWeaveStreaming.h"
#include "engine/world/NereidFloodVisualScene.h"
#if __has_include(<box3d/box3d.h>)
#include "engine/world/TerraWeaveCavernsRimWorldPublication.h"
#endif
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

// Automatic variant: the host passes its observers (camera, players, AI,
// server interest points) instead of hand-computed tiles; the planner keeps
// hysteresis state between frames and orders tiles nearest-first, so the
// per-frame upload budget always fills the ground under the observers first.
// planner.settings().tileSizeCm must equal the coordinator tile size.
inline NativeTerrainPublishResult publishNativeTerraWeaveFrameAuto(
        const World& ecs,NativeTerraWeaveWorldBinding& binding,
        TerrainMutationCoordinator& terrain,
        world::WorldScene& worldScene,gfx::Device& device,
        world::SpatialWorld& spatial,nav::Mesh& navmesh,
        const EarthworksNavSource& navSource,
        TerraWeaveStreamingPlanner& planner,
        std::span<const TerraStreamObserver> observers,
        const TerrainMutationCoordinator::ColliderIdForCell& ids,
        TerraStreamingPlan* planOut=nullptr,
        std::string* navError=nullptr,
        size_t terrainUploadBudget=2u,bool forceRehydrate=false) {
    if(observers.empty())
        throw std::invalid_argument("TerraWeave automatic streaming needs an observer");
    if(int64_t(planner.settings().tileSizeCm)!=
       int64_t(terrain.terrainCellSizeMetres())*100)
        throw std::invalid_argument("TerraWeave streaming tile size differs from terrain tiles");
    TerraStreamingPlan plan=planner.update(observers);
    auto status=publishNativeTerraWeaveFrame(ecs,binding,terrain,worldScene,
        device,spatial,navmesh,navSource,plan.pinnedCells,plan.grassArea,ids,
        navError,terrainUploadBudget,forceRehydrate);
    if(planOut)*planOut=std::move(plan);
    return status;
}

#if __has_include(<box3d/box3d.h>)
// Alternative native driver path while a cave/terrain rim edit is pending.
// Reconcile ECS ground and configureRimWorldFromAuthority BEFORE entering
// this path. No simulation ticks may change the frozen authority until it
// finishes. Ordinary publishNativeTerraWeaveFrame must not run concurrently.
inline caverns::CaveRimWorldProgress publishNativeTerraWeaveRimFrame(
        const World& ecs,NativeTerraWeaveWorldBinding& binding,
        TerrainMutationCoordinator& terrain,world::WorldScene& worldScene,
        caverns::CaveGameplayRuntimeFrame& caves,caverns::CaveTerrainSurfaceBinding& rimBinding,
        gfx::Device& device,physics::Scene& box,world::SpatialWorld& spatial,
        nav::Mesh& surfaceNav,const EarthworksNavSource& surfaceNavSource,
        caverns::CaveNavConfig caveNav,caverns::CaveGameplayEditHost::TerrainHeight height,
        const caverns::CaveGameplayEditHost::ColliderIdAllocator& caveIds,
        const TerrainMutationCoordinator::ColliderIdForCell& terrainIds,uint64_t visualRevision,
        const caverns::CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const std::function<bool(const caverns::CaveSurfaceGroupPlan&)>& nativeOwnersReady,
        size_t cpuJobs=2,size_t terrainUploadBudget=2,caverns::CaveBatchSpec graphics={}){
    const auto* resource=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!resource||!binding.initialized()||binding.loadedStateRevision()!=resource->stateRevision||
       !nativeOwnersReady||!exclusive||!exclusive()){
        caves.quarantineRimPublication();
        throw std::logic_error("TerraWeave rim driver requires frozen reconciled world");
    }
    return caverns::advanceRimWorldPublication(caves,rimBinding,terrain,device,box,spatial,
        surfaceNav,surfaceNavSource,caveNav,std::move(height),caveIds,terrainIds,
        visualRevision,exclusive,[&](const caverns::CaveSurfaceGroupPlan& plan){
            return binding.groundReady()&&binding.publishScene(worldScene)&&nativeOwnersReady(plan);
        },cpuJobs,terrainUploadBudget,graphics);
}
#endif

} // namespace aiverse::terraweave
