#pragma once
// Canonical route epochs for deterministic simulation/replay. The native
// RuntimeFrame is a derived presentation/physics projection and MUST NOT
// supply process-local revisions to persistent lockstep route components.
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"
#include "engine/world/TerraWeaveCavernsNavigationEnvironment.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"

namespace aiverse::terraweave::caverns {
// Capture once at the deterministic system boundary. The supplied residency
// policy must be an immutable deterministic gameplay region, never camera
// demand, native GPU completion, or a mutable asynchronous reader.
inline std::shared_ptr<const CaveNavigationDataProvider>
captureCaveAuthorityNavigation(std::shared_ptr<const World> frozen,
    const CaveAuthorityConfig& cfg,CaveResidencyCheck gameplayRegion,
    uint64_t environmentEpoch=0,CaveEnvironmentCostSettings environment={}) {
    if(!frozen||!gameplayRegion)
        throw std::invalid_argument("Caverns authority navigation region");
    auto snapshot=freezeCaveAuthority(*frozen,cfg);
    auto index=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
    CaveNavigationCallbacks callbacks;
    if(environmentEpoch)
        callbacks=makeCaveEnvironmentCallbacks(
            {snapshot->sourceRevision,environmentEpoch,std::move(frozen)},environment);
    return std::make_shared<const CaveNavigationDataProvider>(
        std::move(index),std::move(callbacks),environmentEpoch,std::move(gameplayRegion));
}
// A bounded fixed gameplay extent (inclusive cm); capture it from immutable
// game configuration or persistent deterministic state, not visual residency.
inline CaveResidencyCheck fixedCaveGameplayRegion(PointCm min,PointCm max) {
    validatePoint(min);validatePoint(max);
    if(min.x>=max.x||min.y>=max.y||min.z>=max.z)
        throw std::invalid_argument("Caverns gameplay region extent");
    return [min,max](PointCm lo,PointCm hi){
        return lo.x>=min.x&&lo.y>=min.y&&lo.z>=min.z&&
               hi.x<=max.x&&hi.y<=max.y&&hi.z<=max.z;
    };
}
} // namespace aiverse::terraweave::caverns
