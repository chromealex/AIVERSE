// Optional GRANULAR -> canonical GameCore sphere edit at the serialized
// completed-tick barrier. The caller opts into the discrete volume policy.
#pragma once
#include "aiverselib/world/GranularPlanetSoilGeometry.h"
#include "aiverselib/world/FracturePlanetFoundation.h"
#include "engine/game/GameCore.h"
#include "engine/game/GameState.h"

namespace aiverse::granular {
inline std::optional<PlanetSoilTransferResult> transferGameCorePlanetSoil(
        game::GameCore& core,World& world,const PlanetSoilRequest& request,
        uint32_t completedTick,int32_t quanta,int16_t bedrock16,int16_t ceiling16,
        std::span<const fracture::PlanetFoundation> foundations={}){
    const auto* gs=world.resourceConst<game::GameState>();
    if(!gs||!gs->surface||!gs->grid.hf||
       gs->planetTerrainRevision!=request.expectedTerrainRevision)return {};
    // Retain the original Surface across the staged World replacement.
    const auto surface=gs->surface;
    World stagedWorld=world;
    auto result=transferPlanetSoil(stagedWorld,*surface,request,completedTick,
        [&](World& staged,const PlanetSoilTarget& target)->std::optional<PlanetSoilOwnerChange>{
            const auto preview=previewPlanetSoilGeometry(*surface,target,quanta,bedrock16,ceiling16);
            if(!preview||!core.stagePlanetTerrain(staged,*surface,target.expectedTerrainRevision,*preview->surface))return {};
            return PlanetSoilOwnerChange{target.expectedTerrainRevision,target.expectedTerrainRevision+1,preview->movedVolumeCm3};
        });
    if(!result)return {};
    if(result->terrain.movedVolumeCm3){
        const auto* current=stagedWorld.resourceConst<game::GameState>();
        const auto nextSurface=current->surface;
        const auto support=fracture::reconcilePlanetFoundations(stagedWorld,*surface,*nextSurface,foundations,completedTick);
        if(!support||!core.reconcilePlanetGroundUnits(stagedWorld,*surface,support->preservePose))return {};
        // Non-Unit native fields also need to remain attached to the soil.
        // Ground Unit fields were already reconciled above; do not move twice.
        if(!stagedWorld.tryGetConst<game::Unit>(request.field)&&
           !std::binary_search(support->preservePose.begin(),support->preservePose.end(),request.field.packed())){
            auto& tr=stagedWorld.get<Transform>(request.field);
            const auto* next=stagedWorld.resourceConst<game::GameState>();
            tr.pos=tr.pos+surface->up(tr.pos)*(next->surface->heightAt(tr.pos)-surface->heightAt(tr.pos));
        }
    }
    world=std::move(stagedWorld);return result;
}
} // namespace aiverse::granular
