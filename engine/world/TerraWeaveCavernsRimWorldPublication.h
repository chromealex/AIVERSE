#pragma once
// No new owner. Host holds reader exclusion for the entire sequence:
// configureRimSurfaceGroup -> ordinary CPU/GPU staging -> exclusive volume
// publication -> bindSurface -> ordinary terrain publication -> finish.
// Box3D surface/GI and all navigation consumers still require real proofs.
#include "engine/world/TerraWeaveCavernsRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsTerrainBinding.h"

namespace aiverse::terraweave::caverns {
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
} // namespace aiverse::terraweave::caverns
