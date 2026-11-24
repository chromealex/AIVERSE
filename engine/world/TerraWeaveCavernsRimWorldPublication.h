#pragma once
// No new owner. Host holds reader exclusion for the entire sequence:
// configureRimSurfaceGroup -> ordinary CPU/GPU staging -> exclusive volume
// publication -> bindSurface -> ordinary terrain publication -> finish.
// Box3D surface/GI and all navigation consumers still require real proofs.
#include "engine/world/TerraWeaveCavernsRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsTerrainBinding.h"

namespace aiverse::terraweave::caverns {
// Owner-side entry point after actual ECS reconcile and cave edit/restore.
// Build one refined surface plan from the canonical frozen authority and
// the existing terrain coordinator's immutable soil snapshot.
inline bool configureRimWorldFromAuthority(const World& frozen,
        const CaveAuthorityConfig& cfg,CaveGameplayRuntimeFrame& frame,
        TerrainMutationCoordinator& terrain,std::span<const world::Cell> pins,
        const std::map<uint64_t,Fixed>& lowerPlanes,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive){
    if(!exclusive||!exclusive()||frame.physicsStatus().recoveryRequired||
       terrain.surfaceEpoch()==UINT64_MAX)return false;
    auto authority=freezeCaveAuthority(frozen,cfg);
    const auto soil=terrain.presentationSnapshot();
    const auto epoch=terrain.surfaceEpoch()+1;
    auto plan=std::make_shared<const CaveSurfaceGroupPlan>(
        refineSurfaceGroupForRimGrid(planCaveSurfaceGroup(*soil,*authority,pins,
            terrain.terrainCellSizeMetres(),terrain.terrainResolution(),epoch),
            frame.volumeResolution(),frame.volumeSpacingCm()));
    if(!exclusive()||terrain.soilRevision()!=plan->soilRevision)return false;
    return frame.configureRimSurfaceGroupAuto(std::move(plan),lowerPlanes,exclusive);
}
inline bool bindCommittedRimSurface(CaveGameplayRuntimeFrame& frame,
        CaveTerrainSurfaceBinding& binding,TerrainMutationCoordinator& terrain,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive){
    if(frame.physicsStatus().recoveryRequired||!frame.host().rimPublicationBlocked())return false;
    return binding.bindCommittedVolumeGroup(frame.host().frozenRimGroup(),terrain,exclusive,
        [&frame](const CaveSurfaceGroupPlan& plan){return frame.host().rimVolumeCommitted(plan);});
}
inline bool finishCommittedRimSurface(CaveGameplayRuntimeFrame& frame,
        const CaveTerrainSurfaceBinding& binding,const TerrainMutationCoordinator& terrain,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const std::function<bool(const CaveSurfaceGroupPlan&)>& boxGiAndNavigationReady){
    if(!boxGiAndNavigationReady||!binding.surfaceReady(terrain))return false;
    return frame.finishRimSurfacePublication([&](const CaveSurfaceGroupPlan& plan){
        return binding.groupPlan().get()==&plan&&binding.surfaceReady(terrain)&&
            boxGiAndNavigationReady(plan);
    },exclusive);
}
enum class CaveRimWorldPhase:uint8_t {
    WaitingForVolume,WaitingForSurface,WaitingForNavigation,WaitingForOwners,Complete,RecoveryRequired
};
struct CaveRimWorldProgress {
    CaveRimWorldPhase phase=CaveRimWorldPhase::WaitingForVolume;
    CaveEditTick volume{};
    size_t surfaceUploads=0,surfacePending=0;
};
// Native owner drives one bounded update at a time; this is not a separate
// scheduler or game module. Keep the actual exclusion lease across calls.
// Readiness callback MUST verify surface Box3D/GI and exact volume-nav
// topology, not simply repeat the surface NavMesh refresh below.
inline CaveRimWorldProgress advanceRimWorldPublication(
        CaveGameplayRuntimeFrame& frame,CaveTerrainSurfaceBinding& binding,
        TerrainMutationCoordinator& terrain,gfx::Device& device,
        physics::Scene& box,world::SpatialWorld& spatial,nav::Mesh& surfaceNav,
        const EarthworksNavSource& surfaceNavSource,CaveNavConfig caveNav,
        CaveGameplayEditHost::TerrainHeight terrainHeight,
        const CaveGameplayEditHost::ColliderIdAllocator& caveIds,
        const TerrainMutationCoordinator::ColliderIdForCell& terrainIds,
        uint64_t visualRevision,const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const std::function<bool(const CaveSurfaceGroupPlan&)>& boxGiAndNavigationReady,
        size_t cpuJobs=2,size_t surfaceUploadBudget=2,CaveBatchSpec graphics={}){
    CaveRimWorldProgress out;
    if(frame.physicsStatus().recoveryRequired){out.phase=CaveRimWorldPhase::RecoveryRequired;return out;}
    const auto plan=frame.host().frozenRimGroup();
    if(!plan||!frame.host().rimPublicationBlocked()||!exclusive||!exclusive()||
       !caveIds||!terrainIds||!terrainHeight||!boxGiAndNavigationReady||!visualRevision)
        throw std::logic_error("Caverns rim publication requires configured group and actual host exclusion");
    try{
        if(!frame.host().rimVolumeCommitted(*plan)){
            out.volume=frame.frameOutsidePass(device,spatial,caveNav,std::move(terrainHeight),caveIds,
                visualRevision,cpuJobs,graphics);
            if(frame.stats().prepared&&!frame.publishWithExclusiveWorld(box,spatial,exclusive)){
                if(frame.physicsStatus().recoveryRequired)out.phase=CaveRimWorldPhase::RecoveryRequired;
                return out;
            }
            if(!frame.host().rimVolumeCommitted(*plan))return out;
        }
        if(!exclusive()||!bindCommittedRimSurface(frame,binding,terrain,exclusive)){
            frame.quarantineRimPublication();out.phase=CaveRimWorldPhase::RecoveryRequired;return out;
        }
        std::vector<world::Cell> pins; pins.reserve(plan->patches.size());
        for(const auto& [key,patch]:plan->patches){(void)patch;pins.push_back(key);}
        out.phase=CaveRimWorldPhase::WaitingForSurface;
        out.surfaceUploads=terrain.publishVisible(device,spatial,pins,surfaceUploadBudget,terrainIds);
        out.surfacePending=terrain.pendingTiles();
        if(!binding.surfaceReady(terrain))return out;
        out.phase=CaveRimWorldPhase::WaitingForNavigation;
        if(!terrain.refreshNavMesh(surfaceNav,surfaceNavSource)||!frame.retryNavigationInvalidations())return out;
        out.phase=CaveRimWorldPhase::WaitingForOwners;
        if(finishCommittedRimSurface(frame,binding,terrain,exclusive,boxGiAndNavigationReady))
            out.phase=CaveRimWorldPhase::Complete;
        return out;
    }catch(...){
        frame.quarantineRimPublication();throw;
    }
}
} // namespace aiverse::terraweave::caverns
