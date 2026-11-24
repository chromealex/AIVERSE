#pragma once
// Opt-in bridge at the actual world owner's exclusive publication boundary.
// Does not create a new terrain, Simulation, SpatialWorld or Box3D scene.
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"
#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include "engine/world/TerraWeaveTerrainRuntime.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
class CaveTerrainSurfaceBinding {
public:
    // Native cave volume must already be staged/committed at the matching
    // authority revision, and the host must keep all relevant readers
    // excluded until surface, volume, Box3D and navigation agree.
    // Same revision on restore requires force=true and a new runtime epoch.
    bool reconcile(const World& frozen,const CaveAuthorityConfig& cfg,
                   TerrainMutationCoordinator& terrain,
                   const std::function<bool()>& exclusive,
                   const std::function<bool(uint64_t)>& volumeReady,
                   bool force=false) {
        if(!exclusive||!exclusive()||!volumeReady)return false;
        if(owner_&&owner_!=&terrain)throw std::logic_error("Caverns surface binding owner changed");
        auto snapshot=freezeCaveAuthority(frozen,cfg);
        if(!volumeReady(snapshot->sourceRevision))return false;
        if(!force&&owner_==&terrain&&authorRevision_==snapshot->sourceRevision&&
           surfaceEpoch_==terrain.surfaceEpoch())return true;
        if(terrain.surfaceEpoch()==UINT64_MAX)throw std::overflow_error("Caverns surface epoch");
        const auto epoch=terrain.surfaceEpoch()+1;
        const auto revision=snapshot->sourceRevision;
        if(!exclusive())return false;
        terrain.setSurfaceTopology(epoch,[snapshot=std::move(snapshot),epoch](TerrainPatch patch){
            return cutCaveTerrainPatch(std::move(patch),*snapshot,epoch);
        });
        owner_=&terrain;authorRevision_=revision;surfaceEpoch_=epoch;
        return true;
    }
    // Surface readiness only. The host must independently prove matching
    // Box3D, underground navigation, GI and native volume publication.
    bool surfaceReady(const TerrainMutationCoordinator& terrain)const noexcept{
        return owner_==&terrain&&terrain.surfaceEpoch()==surfaceEpoch_&&terrain.pendingTiles()==0;
    }
    uint64_t requestedAuthorityRevision()const noexcept{return authorRevision_;}
private:
    const TerrainMutationCoordinator* owner_=nullptr;
    uint64_t authorRevision_=0,surfaceEpoch_=0;
};
} // namespace aiverse::terraweave::caverns
