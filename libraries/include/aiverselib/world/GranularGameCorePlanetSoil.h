// Optional GRANULAR -> canonical GameCore sphere edit at the serialized
// completed-tick barrier. The caller opts into the discrete volume policy.
#pragma once
#include "aiverselib/world/GranularPlanetSoilGeometry.h"
#include "engine/game/GameCore.h"
#include "engine/game/GameState.h"

namespace aiverse::granular {
inline std::optional<PlanetSoilTransferResult> transferGameCorePlanetSoil(
        game::GameCore& core,World& world,const PlanetSoilRequest& request,
        uint32_t completedTick,int32_t quanta,int16_t bedrock16,int16_t ceiling16){
    const auto* gs=world.resourceConst<game::GameState>();
    if(!gs||!gs->surface||!gs->grid.hf||
       gs->planetTerrainRevision!=request.expectedTerrainRevision)return {};
    // Retain the original Surface across the staged World replacement.
    const auto surface=gs->surface;
    return transferPlanetSoil(world,*surface,request,completedTick,
        [&](World& staged,const PlanetSoilTarget& target)->std::optional<PlanetSoilOwnerChange>{
            const auto preview=previewPlanetSoilGeometry(*surface,target,quanta,bedrock16,ceiling16);
            if(!preview||!core.stagePlanetTerrain(staged,*surface,target.expectedTerrainRevision,*preview->surface))return {};
            return PlanetSoilOwnerChange{target.expectedTerrainRevision,target.expectedTerrainRevision+1,preview->movedVolumeCm3};
        });
}
} // namespace aiverse::granular
