// TerraWeave Stage64: connect NEREID flood sampling to persistent,
// deterministic grass-cover recovery. No separate vegetation renderer, no
// per-blade entities and no water solver; only canonical ECS grass patches
// and one small, versioned ShoreRecoveryResource change at an owner barrier.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveShoreRecovery.h"
#include "engine/world/TerraWeaveShorelineRaster.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <aiverselib/water/NereidQueryIndex.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

inline void registerShoreRecoveryPersistence(Simulation& sim) {
    sim.registerPersistentResource<ShoreRecoveryResource>(
        "terraweave.shore_recovery",1);
}

using EcologyClimateOverride=std::function<ShorePatchClimate(
    const GrassPatch&,ShorePatchClimate)>;

// A 5-point equal-area representation of thousands of grass blades. The
// existing NEREID WaterQueryIndex handles river splines, polygons,
// overlapping water bodies and optionally Stage63 flood overlays. Sample
// on the COMPLETED ECS snapshot, never from an async terrain/render worker.
inline ShorePatchClimate sampleShorePatchClimate(
        const GrassPatch& patch,
        const EarthworksLayer& soil,
        const lib::nereid::WaterQueryIndex& water,
        const EcologyClimateOverride& extra={}) {
    (void)prepareGrassPatch(patch);
    const int64_t w=patch.bounds.maxX-patch.bounds.minX;
    const int64_t h=patch.bounds.maxZ-patch.bounds.minZ;
    const int64_t x0=patch.bounds.minX,z0=patch.bounds.minZ;
    const std::array<std::pair<int64_t,int64_t>,5> samples{{
        {x0+w/2,z0+h/2},
        {x0+w/4,z0+h/4},
        {x0+3*w/4,z0+h/4},
        {x0+w/4,z0+3*h/4},
        {x0+3*w/4,z0+3*h/4}
    }};
    uint32_t flooded=0;
    for(const auto& [x,z]:samples){
        const int64_t edge=kWorldLimitCm-soil.spacingCm();
        if(x<=-edge || x>=edge || z<=-edge || z>=edge)
            throw std::out_of_range("TerraWeave ecology sample outside world");
        const int32_t heightCm=soil.sampleHeightCm(x,z);
        const Vec3 sampleAt{
            Fixed::fromRatio(x,100),
            Fixed::fromRatio(heightCm,100),
            Fixed::fromRatio(z,100)};
        const auto resolved=water.sample(sampleAt);
        if(!resolved.found || !resolved.submerged)continue;
        const int64_t surface=resolved.surfaceY.raw*100/Fixed::kOne;
        const int64_t depth=surface-heightCm;
        if(depth>=2){
            ++flooded;
        }
    }
    ShorePatchClimate climate;
    climate.floodedPermille=flooded*200;
    // Take the existing authored moisture/light/fertility until PHYSIS or
    // canopy sampling replaces it explicitly. A flood raises moisture and
    // damages drowning-sensitive grass in the separate ecology kernel.
    climate.waterPermille=std::min<uint32_t>(
        1000,patch.environment.waterPermille+
        (1000-patch.environment.waterPermille)*
        (flooded*200)/1000);
    climate.lightPermille=patch.environment.lightPermille;
    climate.nutrientsPermille=patch.environment.nutrientsPermille;
    return extra?extra(patch,climate):climate;
}

struct ShoreEcologyEcsReport {
    ShoreRecoveryResult recovery{};
    uint64_t stateRevision=0;
};

// Authoritative ECS code: pure data staging followed by ordinary ECS resource
// publication. The host never modifies WorldScene, Box3D or WaterBody from
// here. GrassPatch environment changes are reconstructed by existing
// NativeTerraWeaveWorldBinding on a changed stateRevision.
inline ShoreEcologyEcsReport advanceShoreEcologyEcs(
        World& ecs,const TerraWeaveEcsAuthorityConfig& config,
        uint64_t completedTick,
        ShoreRecoverySettings settings={},
        EcologyClimateOverride extra={}) {
    validateShoreRecoverySettings(settings);
    const auto* authoritative=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!authoritative)
        throw std::invalid_argument("TerraWeave missing canonical ecology ECS state");
    const auto* previous=ecs.resourceConst<ShoreRecoveryResource>();
    ShoreRecoveryResource recovery=previous?
        *previous:ShoreRecoveryResource{};
    if(recovery.initialized){
        if(completedTick<recovery.lastTick)
            throw std::invalid_argument("TerraWeave reversed ecology tick");
        if(completedTick-recovery.lastTick<settings.periodTicks)
            return {};
    }
    auto nextAuthority=*authoritative;
    const auto ground=materializeEcsTerrain(nextAuthority,config);
    const lib::nereid::WaterQueryIndex water(ecs);
    const auto* deadwood=ecs.resourceConst<ForestDeadwoodResource>();
    auto report=advanceShoreRecovery(
        recovery,nextAuthority.grass,completedTick,
        [&](const GrassPatch& patch){
            ShorePatchClimate sample=sampleShorePatchClimate(
                patch,ground,water,extra);
            if(deadwood){
                // Five deterministic aggregate samples, not one ECS
                // record or ecological update per grass blade. A patch
                // may overlap several 32m nutrient cells.
                const int64_t w=patch.bounds.maxX-patch.bounds.minX;
                const int64_t h=patch.bounds.maxZ-patch.bounds.minZ;
                const int64_t x=patch.bounds.minX,z=patch.bounds.minZ;
                const std::array<std::pair<int64_t,int64_t>,5> positions{{
                    {x+w/2,z+h/2},
                    {x+w/4,z+h/4},{x+3*w/4,z+h/4},
                    {x+w/4,z+3*h/4},{x+3*w/4,z+3*h/4}
                }};
                uint32_t total=0;
                for(const auto& point:positions)
                    total+=deadwoodNutrientsPermille(
                        *deadwood,point.first,point.second,
                        sample.nutrientsPermille);
                sample.nutrientsPermille=total/5;
            }
            return sample;
        },settings);
    if(!report.changed.empty()){
        if(nextAuthority.stateRevision==UINT64_MAX)
            throw std::overflow_error("TerraWeave ecology state revision overflow");
        ++nextAuthority.stateRevision;
    }
    const uint64_t revision=nextAuthority.stateRevision;

    // Stage both ECS resources on one World copy: an exception from schema
    // allocation/persistence cannot publish the grass state without its
    // exposure/seed-bank record (or vice versa). The move is a single
    // authoritative structural barrier after all validation passes.
    World staged=ecs;
    // Only generated bank ECS cells are ecology-owned. Manually authored
    // bank armour/litter/shield values remain owned by their own game logic.
    // The same recovery soil-cover state also persists for drying patches,
    // even when their grass blade count is presently zero.
    const ShoreRecoveryCoverIndex cover(nextAuthority.grass,recovery);
    std::vector<std::pair<Entity,uint32_t>> bankUpdates;
    staged.eachExistingConst<AutoShorelineBankComponent>(
        [&](Entity entity,const AutoShorelineBankComponent& marker){
            if(!staged.alive(entity) || marker.generatorVersion!=1)return;
            const auto* vertex=staged.tryGetConst<
                BankEarthVertexComponent>(entity);
            if(!vertex)return;
            const int64_t x=vertex->vertex.x*
                int64_t(config.soilGridCm);
            const int64_t z=vertex->vertex.z*
                int64_t(config.soilGridCm);
            bankUpdates.emplace_back(entity,cover.shieldAt(x,z));
        });
    for(const auto& [entity,shield]:bankUpdates)
        staged.add<lib::nereid::WaterBankSurfaceCoverComponent>(
            entity,lib::nereid::WaterBankSurfaceCoverComponent{shield});
    staged.setResource<ShoreRecoveryResource>(std::move(recovery));
    if(!report.changed.empty())
        staged.setResource<TerraWeaveAuthorityResource>(
            std::move(nextAuthority));
    ecs=std::move(staged);
    return {std::move(report),revision};
}

inline void addTerraWeaveShoreEcologySystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig config,
        ShoreRecoverySettings recovery={},
        EcologyClimateOverride extra={}) {
    validateEcsAuthorityConfig(config);
    validateShoreRecoverySettings(recovery);
    sim.addSystem(std::move(name),
        [config=std::move(config),recovery,extra=std::move(extra)]
        (Simulation& current){
            (void)advanceShoreEcologyEcs(
                current.world(),config,current.tick(),recovery,extra);
        });
}

} // namespace aiverse::terraweave
