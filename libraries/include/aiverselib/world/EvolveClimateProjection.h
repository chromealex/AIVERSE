// Shared ecology projection for flat and planetary consumers. EVOLVE
// limits the original PHYSIS/NEREID/TerraWeave baseline, never increases it.
#pragma once
#include "EvolveEcs.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include <algorithm>
#include <cstdint>
namespace aiverse::evolve {
inline std::uint32_t permille(std::int64_t value){
    value=std::clamp(value,std::int64_t(0),kMaxState);
    return std::uint32_t((value*1000)/kMaxState);
}
inline terraweave::ForestEcologyClimate
projectEvolutionClimate(const Region& region,
                        terraweave::ForestEcologyClimate original){
    if(!valid(region)||!region.enabled)return original;
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
} // namespace aiverse::evolve
