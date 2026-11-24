// EVOLVE -> TerraWeave's actual ForestEcologyClimateSampler.
// Preserves the existing PHYSIS/NEREID climate owner; projects persistent
// region fertility/succession/pollution into the same ecology sampler
// that TerraWeave already accepts. No second forest, terrain or plant store.
#pragma once
#include "aiverselib/world/EvolveEcs.h"
#include "aiverselib/world/EvolveArea.h"
#include "aiverselib/world/EvolveSpatialIndex.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct EvolvePlantRegion {
    Entity region=kNullEntity;
    std::int64_t minXcm=0,minZcm=0,maxXcm=0,maxZcm=0;
    bool enabled=true;
};
inline bool valid(const EvolvePlantRegion& zone){
    return zone.region.valid()&&zone.minXcm<zone.maxXcm&&
           zone.minZcm<zone.maxZcm&&
           zone.minXcm>=-evolve::kAreaCoordinateCm&&
           zone.maxXcm<=evolve::kAreaCoordinateCm&&
           zone.minZcm>=-evolve::kAreaCoordinateCm&&
           zone.maxZcm<=evolve::kAreaCoordinateCm;
}
inline std::uint32_t permille(std::int64_t value){
    value=std::clamp(value,std::int64_t(0),evolve::kMaxState);
    return std::uint32_t((value*1000)/evolve::kMaxState);
}
inline terraweave::ForestEcologyClimate
projectEvolutionClimate(const evolve::Region& region,
                        terraweave::ForestEcologyClimate original){
    if(!evolve::valid(region)||!region.enabled)return original;
    const auto fertility=permille(region.fertility);
    const auto pollution=permille(region.pollution);
    const auto succession=permille(region.succession);
    // Soil nutrients cannot exceed the authoritative host's original
    // PHYSIS/NEREID/biome value; EVOLVE only acts as a limiting factor.
    original.nutrientsPermille=std::min(original.nutrientsPermille,fertility);
    // Long succession shades a region while pollution reduces available
    // light; the original canopy/weather input is never overwritten upward.
    const auto shade=std::min<std::uint32_t>(900,
                                             succession/3+pollution/2);
    original.sunPermille=std::min(original.sunPermille,1000u-shade);
    return original;
}
inline terraweave::ForestEcologyClimateSampler
makeEvolveForestClimateSampler(const World& world,
         std::vector<EvolvePlantRegion> zones,
         terraweave::ForestEcologyClimateSampler baseline={}) {
    if(zones.size()>evolve::kMaxRegions)
        throw std::length_error("EVOLVE forest region projection budget");
    for(const auto& zone:zones)
        if(!valid(zone))throw std::invalid_argument(
            "EVOLVE forest region invalid bounds");
    // Caller must keep World alive while TerraWeave's ecology step
    // synchronously invokes this function. Never store it in an async
    // worker that can outlive World or its rollback snapshot.
    return [&world,zones=std::move(zones),
            baseline=std::move(baseline)](const terraweave::AnchoredPlant& plant){
        auto climate=baseline?baseline(plant):terraweave::ForestEcologyClimate{};
        const auto x=plant.anchor.xCm,z=plant.anchor.zCm;
        for(const auto& zone:zones){
            if(!zone.enabled||x<zone.minXcm||x>=zone.maxXcm||
               z<zone.minZcm||z>=zone.maxZcm)continue;
            const auto* region=world.tryGetConst<evolve::Region>(zone.region);
            if(!region||!region->enabled)continue;
            climate=projectEvolutionClimate(*region,climate);
        }
        return climate;
    };
}
// Canonical, preferred path: derives the TerraWeave sampling rectangles
// directly from persisted EvolveArea ECS. No parallel host-configured region
// rectangles or editor-only radii are needed.
inline terraweave::ForestEcologyClimateSampler
makeEvolveForestClimateSampler(const World& world,
    terraweave::ForestEcologyClimateSampler baseline={}) {
    std::vector<EvolvePlantRegion> zones;
    for(const auto& area:evolve::extents(world))
        zones.push_back({area.region,area.minXcm,area.minZcm,
                         area.maxXcm,area.maxZcm,true});
    if(zones.size()>evolve::kMaxRegions)
        throw std::length_error("EVOLVE forest region projection budget");
    // Bounds are a per-step snapshot. Recheck the authoritative Area before
    // applying it so a removed, disabled or edited area cannot contribute
    // stale climate if the sampler is reused between ecology steps.
    return [&world,zones=std::move(zones),baseline=std::move(baseline)]
           (const terraweave::AnchoredPlant& plant){
        auto climate=baseline?baseline(plant):terraweave::ForestEcologyClimate{};
        const auto x=plant.anchor.xCm,z=plant.anchor.zCm;
        for(const auto& zone:zones){
            if(!world.alive(zone.region))continue;
            const auto* area=world.tryGetConst<evolve::Area>(zone.region);
            const auto* region=world.tryGetConst<evolve::Region>(zone.region);
            if(!area||!area->enabled||!evolve::valid(*area)||
               !region||!region->enabled)continue;
            if(x<area->centerXcm-area->halfWidthCm||
               x>=area->centerXcm+area->halfWidthCm||
               z<area->centerZcm-area->halfDepthCm||
               z>=area->centerZcm+area->halfDepthCm)continue;
            climate=projectEvolutionClimate(*region,climate);
        }
        return climate;
    };
}
// Tick-scoped indexed sampler for synchronous advanceForestEcology.
// The caller must keep region membership and Area bounds fixed for the
// duration of the step (including in baseline callbacks). Rebuild after any
// structural edit or rollback. The public reusable sampler above retains its
// live-area semantics; it cannot use a stale spatial index after edits.
// Scratch is private to the callable: do not invoke the same instance
// concurrently. Region climate values are still read from canonical World.
inline terraweave::ForestEcologyClimateSampler
makeIndexedEvolveForestClimateSampler(const World& world,
    terraweave::ForestEcologyClimateSampler baseline={}) {
    auto zones=evolve::extents(world);
    if(zones.size()>evolve::kMaxRegions)
        throw std::length_error("EVOLVE forest region projection budget");
    return [&world,index=evolve::SpatialIndex(std::move(zones)),
            baseline=std::move(baseline),scratch=std::vector<size_t>{}]
           (const terraweave::AnchoredPlant& plant) mutable {
        auto climate=baseline?baseline(plant):terraweave::ForestEcologyClimate{};
        index.query(plant.anchor.xCm,plant.anchor.zCm,scratch,
            [&](const evolve::Extent& zone){
                if(!world.alive(zone.region))return;
                const auto* region=world.tryGetConst<evolve::Region>(zone.region);
                if(region)climate=projectEvolutionClimate(*region,climate);
            });
        return climate;
    };
}
} // namespace aiverse::examples::world_processes_native
