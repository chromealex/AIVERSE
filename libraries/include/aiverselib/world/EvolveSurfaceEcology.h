// Planetary ecology query for a host that already owns the plant/climate
// sampler. Explicit 3D simulation-space position; do not reinterpret a
// legacy AnchoredPlant's planar xCm/zCm as a spherical position.
#pragma once
#include "EvolveSurfaceRegions.h"
#include "EvolveClimateProjection.h"

namespace aiverse::evolve {
inline terraweave::ForestEcologyClimate sampleSurfaceEcology(
        const World& world,const game::Surface& surface,
        const SurfaceRegionIndex& index,Vec3 position,
        terraweave::ForestEcologyClimate baseline){
    index.query(world,surface,position,[&](Entity,const Region& region){
        baseline=projectEvolutionClimate(region,baseline);
    });
    return baseline;
}
} // namespace aiverse::evolve
