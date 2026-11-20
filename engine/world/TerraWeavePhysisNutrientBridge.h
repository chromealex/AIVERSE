// TerraWeave Stage69 — optional compile-time PHYSIS weather source adapter.
// No PHYSIS simulation is cloned. The read-only host World must already
// contain a completed authoritative PHYSIS climate tick before TerraWeave
// consumes it. This header compiles without PHYSIS in terrain-only builds.
#pragma once
#include "engine/world/TerraWeaveForestNutrientEcs.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

#if __has_include("engine/physis/PhysisPresentation.h")
#include "engine/physis/PhysisPresentation.h"
#define AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE 1
#else
#define AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE 0
#endif

namespace aiverse::terraweave {

inline constexpr bool terraweavePhysisWeatherAvailable() {
    return AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE != 0;
}

#if AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE
inline physis::NatureFieldSample requireTerraWeavePhysisSample(
        const World& source,int64_t xCm,int64_t zCm) {
    if(xCm<=-kWorldLimitCm || xCm>=kWorldLimitCm ||
       zCm<=-kWorldLimitCm || zCm>=kWorldLimitCm)
        throw std::out_of_range("TerraWeave PHYSIS climate world extent");
    physis::NatureFieldSample sample;
    // 1m above a generic 0m plane is sufficient for XZ atmosphere grid
    // selection; the optional PHYSIS host may replace this sampler when
    // it needs a true authored-ground contact elevation.
    if(!physis::sampleNatureField(
            source,{Fixed::fromRatio(xCm,100),
                    Fixed{},Fixed::fromRatio(zCm,100)},sample) ||
       !sample.hasAtmosphere)
        throw std::logic_error(
            "TerraWeave PHYSIS weather requires active atmosphere");
    return sample;
}

inline uint32_t physisClampedPermille(Fixed value) {
    const int64_t q=std::clamp<int64_t>(
        value.raw,0,Fixed::kOne);
    return uint32_t(q*1000/Fixed::kOne);
}
inline int32_t physisTemperatureCentiC(Fixed value) {
    const int64_t q=std::clamp<int64_t>(
        value.raw,-80ll*Fixed::kOne,70ll*Fixed::kOne);
    return int32_t(q*100/Fixed::kOne);
}
#endif

inline ForestDeadwoodClimateSampler makeTerraWeavePhysisDecayClimate(
        const World& world) {
#if AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE
    return [&world](const ForestDeadwoodRecord& wood){
        const auto sample=requireTerraWeavePhysisSample(
            world,wood.xCm,wood.zCm);
        return ForestDeadwoodClimate{
            physisTemperatureCentiC(sample.environment.temperature),
            physisClampedPermille(sample.environment.wetness)};
    };
#else
    (void)world;
    throw std::logic_error(
        "TerraWeave PHYSIS weather not built in this source configuration");
#endif
}

inline ForestNutrientWeatherSampler makeTerraWeavePhysisNutrientWeather(
        const World& world) {
#if AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE
    return [&world](int64_t xCm,int64_t zCm){
        const auto sample=requireTerraWeavePhysisSample(
            world,xCm,zCm);
        // PHYSIS rain is normalized precipitation intensity (not a
        // physical water volume). NEREID continues to own actual water.
        return ForestNutrientWeather{
            physisClampedPermille(sample.environment.rain),
            physisClampedPermille(sample.environment.wetness)};
    };
#else
    (void)world;
    throw std::logic_error(
        "TerraWeave PHYSIS weather not built in this source configuration");
#endif
}

} // namespace aiverse::terraweave

#undef AIVERSE_TERRAWEAVE_HAS_PHYSIS_NATURE
