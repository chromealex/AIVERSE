// TerraWeave opt-in native host read barrier. ECS World is authoritative;
// this adapter rehydrates a transient terrain/vegetation cache after ticks,
// save loads, and Simulation rollback, then publishes the SAME resource
// revision into WorldScene near forest and Horizon when GPU tiles are ready.
// No NEREID flow duplication and no gameplay writes from rendering.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include "engine/world/TerraWeaveTerrainRuntime.h"
#include "engine/world/TerraWeaveWorldSceneBinding.h"
#include "engine/world/TerraWeaveForestScene.h"
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

class NativeTerraWeaveWorldBinding {
public:
    NativeTerraWeaveWorldBinding(
            TerraWeaveEcsAuthorityConfig cfg,EarthworksLayer& earth,
            RootSupportRegistry& roots,GrassCoverRegistry& grass,
            TerrainMutationCoordinator& runtime)
        :cfg_(std::move(cfg)),earth_(earth),roots_(roots),
         grass_(grass),runtime_(runtime) {
        validateEcsAuthorityConfig(cfg_);
        if(earth_.spacingCm()!=cfg_.soilGridCm ||
           earth_.bedrockCm()!=cfg_.bedrockCm)
            throw std::invalid_argument(
                "TerraWeave native host ground grid differs from ECS authority");
    }

    uint64_t loadedStateRevision()const{return stateRevision_;}
    bool initialized()const{return initialized_;}

    // Call after completed simulation tick / state-restored hook and before
    // any render/Box3D/navmesh owner phase. Rehydrate when stateRevision
    // changes; an explicit force=true handles an ECS rollback to the SAME
    // revision with different content (branches/replays).
    bool reconcile(const World& ecs,world::WorldScene& scene,
                   bool force=false) {
        const auto* state=ecs.resourceConst<TerraWeaveAuthorityResource>();
        if(!state || state->immutableRecipeId!=cfg_.immutableRecipeId)
            throw std::invalid_argument(
                "TerraWeave scene authority resource/recipe mismatch");
        if(initialized_ && !force && state->stateRevision==stateRevision_)
            return false;

        // Ecology-only stateRevision updates must NOT rebuild all terrain
        // RHI/SpatialWorld/Box3D heightfields. Only a changed soil revision
        // or an explicit rollback/force invalidates resident ground.
        const bool groundChanged=
            !initialized_ || force ||
            state->earth.revision!=earth_.revision();
        std::optional<EarthworksLayer> stagedSoil;
        if(groundChanged)
            stagedSoil.emplace(materializeEcsTerrain(*state,cfg_));
        bool grassChanged=state->grass.size()!=grass_.size();
        if(!grassChanged)
            for(const auto& patch:state->grass){
                const auto* old=grass_.find(patch.key);
                if(!old || !sameGrassPresentation(*old,patch)){
                    grassChanged=true;
                    break;
                }
            }
        RootSupportRegistry stagedRoots;
        GrassCoverRegistry stagedGrass;
        stagedRoots.setSnapshot(state->rooted);
        stagedGrass.setSnapshot(state->grass);

        // Construct view metadata before modifying the live scene or soil.
        std::vector<RootState> exposed;
        std::vector<ForestPlantCondition> conditions;
        std::vector<ForestTreeIdentity> fallen;
        exposed.reserve(state->rooted.size());
        conditions.reserve(state->rooted.size());
        fallen.reserve(state->rooted.size());
        for(const auto& plant:state->rooted){
            const ForestTreeIdentity id{
                plant.anchor.plantKey,plant.anchor.speciesId};
            if(plant.support.plantKey)exposed.push_back(plant.support);
            conditions.push_back({id,plant.environment});
            if(plant.fallen)fallen.push_back(id);
        }
        std::vector<ForestTreeIdentity> dead;
        std::vector<FoliageInstance> recruited;
        if(const auto* ecology=ecs.resourceConst<ForestEcologyResource>()){
            validateForestEcologyResource(*ecology);
            dead.reserve(ecology->plants.size());
            std::map<AnchoredPlantId,const AnchoredPlant*> rootsById;
            for(const auto& plant:state->rooted)
                rootsById.emplace(
                    AnchoredPlantId{plant.anchor.speciesId,
                                    plant.anchor.plantKey},&plant);
            for(const auto& record:ecology->plants){
                if(record.dead)
                    dead.push_back({record.id.key,record.id.speciesId});
                if(!record.recruited)continue;
                const auto match=rootsById.find(record.id);
                if(match==rootsById.end())continue;
                const auto& p=*match->second;
                FoliageInstance instance;
                instance.key=p.anchor.plantKey;
                instance.speciesId=p.anchor.speciesId;
                instance.xCm=p.anchor.xCm;
                instance.zCm=p.anchor.zCm;
                instance.heightCm=p.anchor.plantedHeightCm;
                instance.habit=p.anchor.roots.habit;
                instance.yawMilliDegrees=uint32_t(
                    detail::mix64(instance.key^(uint64_t(instance.speciesId)<<32))%
                    360000ull);
                instance.scalePermille=1000;
                instance.ageTicks=p.environment.ageTicks;
                instance.matureTicks=p.environment.matureTicks;
                instance.lifespanTicks=p.environment.lifespanTicks;
                instance.lightPermille=p.environment.lightPermille;
                instance.waterPermille=p.environment.waterPermille;
                instance.nutrientsPermille=p.environment.nutrientsPermille;
                recruited.push_back(instance);
            }
        }

        // Validate the entire harvested set BEFORE exchanging live soil,
        // roots or grass. A malformed saved tombstone must never produce a
        // partially published frame.
        const auto* harvest=
            ecs.resourceConst<ForestHarvestResource>();
        const bool harvestChanged=harvest &&
            (!initialized_ || force || !harvestRevision_ ||
             *harvestRevision_!=harvest->revision);
        std::optional<std::vector<ForestTreeIdentity>> harvestedIds;
        if(harvestChanged){
            validateForestHarvestResource(*harvest);
            harvestedIds.emplace();
            harvestedIds->reserve(harvest->records.size());
            for(const auto& record:harvest->records)
                harvestedIds->push_back({
                    record.id.key,record.id.speciesId});
        }

        // Stage publication; the render/physics system is not authoritative
        // and can be rebuilt after successful ECS snapshot validation.
        if(stagedSoil)
            earth_.exchangeSnapshotState(*stagedSoil);
        roots_.exchangeSnapshotState(stagedRoots);
        if(grassChanged)
            grass_.exchangeSnapshotState(stagedGrass);
        if(auto* forest=scene.forestScene()){
            forest->setPlantConditions(conditions);
            forest->setRootStates(exposed);
            forest->setFallenTrees(fallen);
            forest->setDeadTrees(dead);
            forest->setRecruitedPlants(recruited);
            // Legacy games may still own presentation-only removals.
            // Only an opt-in persistent harvest resource replaces them.
            if(harvestedIds)
                forest->setRemovedTrees(*harvestedIds);
        }

        // The caller now uploads new dynamic mesh/collision tiles within
        // its per-frame budget. Do not simulate actor movement on stale
        // ground while pendingTiles()>0.
        if(groundChanged){
            runtime_.invalidateAllResident();
            world_.reset(scene);
        }else if(grassChanged){
            // Only grass changes need rebatching. Tree/shrub age/light and
            // mortality use existing Arbor shared prototypes and per-tree
            // conditions; no terrain or grass buffer reupload is required.
            runtime_.grassRenderer().invalidateAll();
        }
        stateRevision_=state->stateRevision;
        harvestRevision_=harvest?
            std::optional<uint64_t>(harvest->revision):std::nullopt;
        initialized_=true;
        return true;
    }

    // After publishVisible(), call this before WorldScene::update/draw.
    // The snapshot goes live only after pending tile publications finish.
    bool publishScene(world::WorldScene& scene){
        if(!initialized_)return false;
        return world_.synchronize(scene,runtime_);
    }
    bool groundReady()const{return initialized_ &&
                                    runtime_.pendingTiles()==0;}
    // Host must stop submitting old dynamic terrain while resetting.
    void detach(world::WorldScene& scene){
        world_.reset(scene);
        initialized_=false;
        stateRevision_=0;
        harvestRevision_.reset();
    }

private:
    TerraWeaveEcsAuthorityConfig cfg_;
    EarthworksLayer& earth_;
    RootSupportRegistry& roots_;
    GrassCoverRegistry& grass_;
    TerrainMutationCoordinator& runtime_;
    WorldSceneEarthworksBinding world_;
    uint64_t stateRevision_=0;
    std::optional<uint64_t> harvestRevision_;
    bool initialized_=false;
};

} // namespace aiverse::terraweave
