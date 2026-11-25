#pragma once
// Rebuild external SpatialWorld and graph owners from the restored ECS
// authoritative leaf snapshot. This never touches GPU or client transport.
#include "engine/buildings/BuildingEcsRuntime.h"
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

namespace aiverse::building {

// All scene/graph owners MUST be new and unused, with collision ID leases
// reserved by the actual shared SceneCollisionNamespaces owner. Rebuild
// happens only at a world structural-change barrier after rollback.
// A failed step keeps the BuildingEcsRuntime suspended and returns false.
inline bool restoreBuildingEcsLeafScene(
    Simulation& sim,
    BuildingEcsRuntime& runtime,
    BuildingDualLeafOwner& freshPhysics,
    BuildingDualLeafGameSession& freshSession,
    world::SpatialWorld& spatial,
    BuildingGraphChangeRouter& freshGraphs,
    const std::vector<BuildingAperturePose>& authoredPoses,
    const std::map<std::string,BuildingDualLeafIds>& reservedIds,
    BuildingDualLeafOwner::SweptObstruction probe,
    const BuildingStyle& style,
    BuildingDualLeafGameSession::Authorize authorize,
    BuildingDualLeafGameSession::InteractionRange reachable,
    uint32_t graphDomains,
    std::string* error=nullptr,const world::Bounds* fullSceneBounds=nullptr){
    auto fail=[&](const std::string& reason){
        if(error)*error=reason;
        return false;
    };
    if(!runtime.suspended()||freshPhysics.attached()||
       freshSession.attached()||!probe||!authorize||!reachable||
       graphDomains==0||(graphDomains&~kBuildingGraphAll))
        return fail("Restoration requires suspended ECS owner and fresh authorized world owners");
    auto* state=buildingEcsSceneState(sim.world(),runtime.sceneId());
    if(!state||!state->sceneId||(!fullSceneBounds&&state->leaves.empty())||
       state->leaves.size()!=authoredPoses.size()||
       reservedIds.size()!=authoredPoses.size()||
       authoredPoses.size()>64)
        return fail("Restored ECS leaf snapshot does not match authored scene");
    std::map<std::string,uint16_t> positions;
    for(const auto& entry:state->leaves){
        if(entry.socketId.empty()||entry.physicalPermille>1000||
           !positions.emplace(entry.socketId,entry.physicalPermille).second||
           entry.open!=(entry.physicalPermille==1000))
            return fail("Invalid restored physical progress or duplicate socket");
    }
    for(const auto& pose:authoredPoses)
        if(!positions.contains(pose.socketId))
            return fail("Restored and authored sockets differ");
    // The session's old commands and graph nonces are gone; this is an
    // explicit external-owner reconstruction, never a replay of actions.
    const uint64_t newRevision=std::max<uint64_t>(1,state->revision);
    if(!freshPhysics.attachRestored(authoredPoses,reservedIds,positions,
            newRevision,spatial,state->sceneId,std::move(probe),error))
        return false;
    // One conservative graph invalidation covers all full closed extents;
    // real graph consumers (including BuildingDynamicNavSource) must be
    // registered on the new router before calling this helper.
    // Moving panels slide OUTSIDE the closed aperture. Invalidate the
    // entire possible displacement for both leaves, not just the
    // original opening, or old remote nav cells could remain stale.
    auto fullTravel=[](const world::Bounds& original){
        auto expanded=original;
        const Fixed dx=original.max.x-original.min.x;
        const Fixed dz=original.max.z-original.min.z;
        if(dx>=dz){
            const Fixed shift=dx/2;
            expanded.min.x-=shift;
            expanded.max.x+=shift;
        }else{
            const Fixed shift=dz/2;
            expanded.min.z-=shift;
            expanded.max.z+=shift;
        }
        return expanded;
    };
    world::Bounds affected=fullSceneBounds?*fullSceneBounds:
        fullTravel(authoredPoses.front().bounds);
    for(size_t i=0;i<authoredPoses.size();++i){
        const auto b=fullTravel(authoredPoses[i].bounds);
        affected.min={fxMin(affected.min.x,b.min.x),
                      fxMin(affected.min.y,b.min.y),
                      fxMin(affected.min.z,b.min.z)};
        affected.max={fxMax(affected.max.x,b.max.x),
                      fxMax(affected.max.y,b.max.y),
                      fxMax(affected.max.z,b.max.z)};
    }
    const BuildingGraphChange invalidation{
        state->sceneId,newRevision,UINT32_MAX,
        BuildingGraphChangeKind::StructureChanged,
        graphDomains,affected,false};
    const auto acknowledged=freshGraphs.publish(invalidation);
    if(!acknowledged.complete){
        if(error)*error=acknowledged.error.empty()?
            "Restored graph invalidation not acknowledged":
            acknowledged.error;
        freshPhysics.detach(spatial);
        freshGraphs.forgetScene(state->sceneId);
        return false;
    }
    if(!freshSession.attach(state->sceneId,freshPhysics,style,
                            std::move(authorize),std::move(reachable),error)){
        freshPhysics.detach(spatial);
        freshGraphs.forgetScene(state->sceneId);
        return false;
    }
    // Restored partially-open leaves are intentionally stationary. The
    // next authenticated command may explicitly finish opening/closing.
    // Set metadata only after collision + all graph owners have committed.
    const BuildingEcsWorldState original=*state;
    state->revision=newRevision;
    state->graphAcknowledged=true;
    for(auto& leaf:state->leaves){
        leaf.revision=newRevision;
        leaf.moving=false;
    }
    if(!runtime.rebindAfterRestore(freshSession,freshPhysics,spatial,
                                   freshGraphs,error)){
        *state=original; // Do not corrupt the restored authoritative World.
        freshSession.detach();
        freshPhysics.detach(spatial);
        freshGraphs.forgetScene(state->sceneId);
        return false;
    }
    if(error)error->clear();
    return true;
}

} // namespace aiverse::building
