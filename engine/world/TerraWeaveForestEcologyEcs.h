// TerraWeave Stage65 — genre-independent authoritative rooted forest ecology.
// Uses existing NEREID WaterQueryIndex + TerraWeave ground as immutable inputs.
// No new water/physics/forest renderer and no ECS entity per cosmetic tree.
// Stable updates are persisted through the current World COW resource path.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

inline void registerTerraWeaveForestEcologyPersistence(Simulation& sim){
    sim.registerPersistentResource<ForestEcologyResource>(
        "terraweave.forest_ecology",1);
}

using ForestEcologyClimateOverride=std::function<
    ForestEcologyClimate(const AnchoredPlant&,ForestEcologyClimate)>;

struct ForestEcologyEcsOutcome {
    ForestEcologyResult ecology{};
    uint64_t authoritativeStateRevision=0;
};

// Authoritative host chooses sunlight/nutrients from PHYSIS, canopy/GI or
// map authoring. Existing water bodies including Stage63 flood columns
// are sampled once for each processed plant at its actual root-zone height.
// A submerged root zone raises water exposure; this is not a second water
// solver. Reference vs indexed source selection stays in NEREID.
inline ForestEcologyClimate nativeRootedPlantClimate(
        const AnchoredPlant& plant,
        const EarthworksLayer& soil,
        const lib::nereid::WaterQueryIndex& water,
        const ForestEcologyClimateOverride& extra={}) {
    const int64_t edge=kWorldLimitCm-soil.spacingCm();
    if(plant.anchor.xCm<=-edge || plant.anchor.xCm>=edge ||
       plant.anchor.zCm<=-edge || plant.anchor.zCm>=edge)
        throw std::out_of_range("TerraWeave rooted ecology world boundary");
    const int32_t ground=soil.sampleHeightCm(
        plant.anchor.xCm,plant.anchor.zCm);
    const Vec3 at{Fixed::fromRatio(plant.anchor.xCm,100),
                  Fixed::fromRatio(ground,100),
                  Fixed::fromRatio(plant.anchor.zCm,100)};
    const auto sample=water.sample(at);
    ForestEcologyClimate c;
    c.sunPermille=1000; // external sun policy may override
    c.waterPermille=plant.environment.waterPermille;
    c.nutrientsPermille=plant.environment.nutrientsPermille;
    if(sample.found && sample.submerged){
        c.floodedPermille=1000;
        c.waterPermille=1000;
    }
    return extra?extra(plant,c):c;
}

// Runs only when due (typically once every 32 fixed ticks). A deterministic
// cursor selects at most 256 rooted trees/shrubs for expensive light, water,
// age/mortality and recruitment decisions, irrespective of total population.
// Index construction copies/visits the opted-in <=65536 rooted subset at
// each due step: it is O(N) per ecology period, NOT O(1) per frame.
inline ForestEcologyEcsOutcome advanceForestEcologyEcs(
        World& ecs,
        const TerraWeaveEcsAuthorityConfig& cfg,
        uint64_t tick,
        const std::vector<ForestEcologySpecies>& species,
        ForestEcologySettings settings={},
        ForestEcologyClimateOverride extra={}) {
    validateForestEcologySettings(settings);
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!authority)
        throw std::logic_error("TerraWeave forest ECS has no root authority");
    const auto* previous=ecs.resourceConst<ForestEcologyResource>();
    ForestEcologyResource ecology=previous?
        *previous:ForestEcologyResource{};
    if(ecology.initialized){
        if(tick<ecology.lastStepTick)
            throw std::invalid_argument("TerraWeave forest ECS reversed tick");
        if(tick-ecology.lastStepTick<settings.intervalTicks)
            return {};
    }
    // No terrain/physics mutation. The same saved soil snapshot is used
    // to sample NEREID water and place any viable new native sapling.
    // Materialized lazily: a step that evaluates no plant (empty or fully
    // dead/fallen population) does not rebuild terrain or the water index.
    std::optional<EarthworksLayer> groundCache;
    std::optional<lib::nereid::WaterQueryIndex> waterCache;
    const auto ground=[&]()->const EarthworksLayer&{
        if(!groundCache)groundCache.emplace(materializeEcsTerrain(*authority,cfg));
        return *groundCache;
    };
    const auto water=[&]()->const lib::nereid::WaterQueryIndex&{
        if(!waterCache)waterCache.emplace(ecs);
        return *waterCache;
    };
    auto rooted=authority->rooted;
    // A native caller must explicitly restore a harvested ID before
    // registering new roots with that identity. Otherwise a hidden tree
    // could keep producing seeds or shade live neighbours.
    if(const auto* harvested=
           ecs.resourceConst<ForestHarvestResource>()){
        validateForestHarvestResource(*harvested);
        for(const auto& plant:rooted){
            if(isForestHarvested(*harvested,{
                    plant.anchor.speciesId,plant.anchor.plantKey}))
                throw std::logic_error(
                    "TerraWeave harvested rooted plant must be restored");
        }
    }
    const auto* deadwood=
        ecs.resourceConst<ForestDeadwoodResource>();
    ForestEcologyResult result=advanceForestEcology(
        ecology,rooted,tick,species,
        [&ground](int64_t x,int64_t z)->std::optional<int32_t>{
            const int64_t edge=kWorldLimitCm-ground().spacingCm();
            if(x<=-edge || x>=edge || z<=-edge || z>=edge)
                return std::nullopt;
            return ground().sampleHeightCm(x,z);
        },
        [&](const AnchoredPlant& plant){
            ForestEcologyClimate sampled=
                nativeRootedPlantClimate(plant,ground(),water(),extra);
            // Mineralised organic matter is a local fertility PROXY, not
            // a duplicate soil height/erosion material. It supplements
            // the authored or PHYSIS baseline without creating mass.
            if(deadwood)
                sampled.nutrientsPermille=deadwoodNutrientsPermille(
                    *deadwood,plant.anchor.xCm,plant.anchor.zCm,
                    sampled.nutrientsPermille);
            return sampled;
        },settings);

    // Natural mortality registers organic matter exactly once, as part
    // of the same World COW snapshot as ecology death/age changes.
    // If deadwood is disabled, older worlds keep their prior semantics.
    std::optional<ForestDeadwoodResource> stagedDeadwood;
    if(deadwood && !result.died.empty()){
        stagedDeadwood=*deadwood;
        for(const auto& death:result.died){
            const auto found=std::find_if(rooted.begin(),rooted.end(),
                [&](const AnchoredPlant& plant){
                    return plant.anchor.speciesId==death.id.speciesId &&
                           plant.anchor.plantKey==death.id.key;
                });
            if(found==rooted.end())
                throw std::logic_error(
                    "TerraWeave natural death missing rooted identity");
            seedForestDeadwood(*stagedDeadwood,*found,tick);
        }
    }
    TerraWeaveAuthorityResource updated=*authority;
    const bool changed=!result.changed.empty() ||
                       !result.died.empty() ||
                       !result.born.empty();
    if(changed){
        if(updated.stateRevision==UINT64_MAX)
            throw std::overflow_error("TerraWeave forest ECS resource revision overflow");
        ++updated.stateRevision;
        updated.rooted=std::move(rooted);
    }
    // Commit BOTH persistent histories and mutated plant environment through
    // a staged World copy. A partial exception never publishes the new age
    // without the shade-stress/death/recruitment record for that same tick.
    World staged=ecs;
    staged.setResource<ForestEcologyResource>(std::move(ecology));
    if(changed)
        staged.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    if(stagedDeadwood)
        staged.setResource<ForestDeadwoodResource>(
            std::move(*stagedDeadwood));
    const uint64_t revision=changed?
        authority->stateRevision+1:authority->stateRevision;
    ecs=std::move(staged);
    return {std::move(result),revision};
}

inline void addTerraWeaveForestEcologySystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg,
        std::vector<ForestEcologySpecies> species,
        ForestEcologySettings settings={},
        ForestEcologyClimateOverride climate={},
        std::function<void(const ForestEcologyEcsOutcome&)> onChanged={}) {
    validateEcsAuthorityConfig(cfg);
    validateForestEcologySettings(settings);
    if(species.empty() || species.size()>64u)
        throw std::invalid_argument(
            "TerraWeave forest ecology needs 1..64 authored species policies");
    for(const auto& p:species)validateForestEcologySpecies(p);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),species=std::move(species),
         settings,climate=std::move(climate),
         onChanged=std::move(onChanged)](Simulation& current){
            const auto result=advanceForestEcologyEcs(
                current.world(),cfg,current.tick(),species,settings,climate);
            // Host callbacks must only enqueue the immutable outcome to
            // post-tick presentation; no Box3D or Renderer3D calls here.
            if(onChanged && (!result.ecology.changed.empty() ||
                             !result.ecology.died.empty() ||
                             !result.ecology.born.empty()))
                onChanged(result);
        });
}

} // namespace aiverse::terraweave
