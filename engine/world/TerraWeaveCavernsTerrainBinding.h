#pragma once
// Opt-in bridge at the actual world owner's exclusive publication boundary.
// Does not create a new terrain, Simulation, SpatialWorld or Box3D scene.
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"
#include "engine/world/TerraWeaveCavernsSurfaceGroup.h"
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
        if(!force&&!groupPlan_&&owner_==&terrain&&authorRevision_==snapshot->sourceRevision&&
           surfaceEpoch_==terrain.surfaceEpoch())return true;
        if(terrain.surfaceEpoch()==UINT64_MAX)throw std::overflow_error("Caverns surface epoch");
        const auto epoch=terrain.surfaceEpoch()+1;
        const auto revision=snapshot->sourceRevision;
        if(!exclusive())return false;
        terrain.setSurfaceTopology(epoch,[snapshot=std::move(snapshot),epoch](TerrainPatch patch){
            return cutCaveTerrainPatch(std::move(patch),*snapshot,epoch);
        });
        owner_=&terrain;authorRevision_=revision;surfaceEpoch_=epoch;groupPlan_.reset();
        return true;
    }
    // Full preflight before queueing any native mutation. The supplied
    // pins are the actual required surface cells, not the camera frustum.
    // Hold exclusion across all budgeted publishVisible calls. Any pin/soil
    // change requires a fresh plan; force=true on restored ECS timelines.
    bool reconcileGroup(const World& frozen,const CaveAuthorityConfig& cfg,
            TerrainMutationCoordinator& terrain,std::span<const world::Cell> pinned,
            const std::function<bool()>& exclusive,
            const std::function<bool(uint64_t)>& volumeReady,bool force=false){
        if(!exclusive||!exclusive()||!volumeReady)return false;
        if(owner_&&owner_!=&terrain)throw std::logic_error("Caverns surface binding owner changed");
        auto snapshot=freezeCaveAuthority(frozen,cfg);
        if(!volumeReady(snapshot->sourceRevision))return false;
        std::set<world::Cell> cells(pinned.begin(),pinned.end());
        if(cells.size()!=pinned.size()||cells.empty()||cells.size()>64)
            throw std::invalid_argument("Caverns surface group pin set");
        if(!force&&groupPlan_&&owner_==&terrain&&authorRevision_==snapshot->sourceRevision&&
           surfaceEpoch_==terrain.surfaceEpoch()&&groupPlan_->soilRevision==terrain.soilRevision()&&
           cells.size()==groupPlan_->patches.size()&&std::all_of(cells.begin(),cells.end(),
                [&](const auto& cell){return groupPlan_->patches.contains(cell);}))return true;
        if(terrain.surfaceEpoch()==UINT64_MAX)throw std::overflow_error("Caverns surface epoch");
        const auto epoch=terrain.surfaceEpoch()+1;
        const auto soil=terrain.presentationSnapshot();
        auto plan=std::make_shared<const CaveSurfaceGroupPlan>(planCaveSurfaceGroup(*soil,*snapshot,pinned,
            terrain.terrainCellSizeMetres(),terrain.terrainResolution(),epoch));
        if(!exclusive()||!volumeReady(snapshot->sourceRevision))return false;
        terrain.setSurfaceGroupTopology(epoch,plan->soilRevision,std::move(cells),[plan](TerrainPatch source){
            const auto it=plan->patches.find(source.cell);
            if(it==plan->patches.end()||source.sourceRevision!=plan->soilRevision||
               source.heightsCm!=it->second.heightsCm||source.resolution!=plan->resolution||
               source.cellSizeMetres!=plan->tileMetres)
                throw std::logic_error("Caverns stale surface group source");
            return it->second;
        },exclusive);
        owner_=&terrain;authorRevision_=snapshot->sourceRevision;surfaceEpoch_=epoch;
        groupPlan_=std::move(plan);return true;
    }
    std::shared_ptr<const CaveSurfaceGroupPlan> groupPlan()const noexcept{return groupPlan_;}
    // Surface readiness only. The host must independently prove matching
    // Box3D, underground navigation, GI and native volume publication.
    bool surfaceReady(const TerrainMutationCoordinator& terrain)const noexcept{
        return owner_==&terrain&&terrain.surfaceEpoch()==surfaceEpoch_&&terrain.pendingTiles()==0&&terrain.surfaceGroupReady();
    }
    uint64_t requestedAuthorityRevision()const noexcept{return authorRevision_;}
private:
    std::shared_ptr<const CaveSurfaceGroupPlan> groupPlan_;
    const TerrainMutationCoordinator* owner_=nullptr;
    uint64_t authorRevision_=0,surfaceEpoch_=0;
};
} // namespace aiverse::terraweave::caverns
