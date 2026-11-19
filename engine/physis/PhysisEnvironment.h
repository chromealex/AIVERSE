// PHYSIS Stage 3: data-only environmental responses for native projects.
// Deliberately no Terra, Water, VFX, Physics, FPS or RTS dependencies.
#pragma once
#include "PhysisClimate.h"

namespace aiverse::physis {

// Optional per-cell physical material. An asset/importer may configure these
// from a terrain biome, snow surface or water shore; PHYSIS doesn't own assets.
struct SurfaceMaterial {
    SurfaceParameters simulation{};
    Fixed permeability = Fixed::fromRatio(1, 2); // 0 impervious, 1 permeable
    Fixed tractionDry = Fixed::fromInt(1);        // dimensionless relative multiplier
};

// Portable read-only adapter for physics, foliage, water, audio and render FX.
struct EnvironmentalResponse {
    Fixed temperature{};
    Fixed humidity{};
    Vec2 wind{};
    Fixed rain{};
    Fixed snow{};
    Fixed wetness{};
    Fixed snowpack{};
    Fixed ice{};
    Fixed tractionMultiplier = Fixed::fromInt(1);
    Fixed runoffPotential{};   // normalized, not water volume
    Fixed fireSuppression{};   // rain + surface wetness factor, normalized
};

inline SurfaceParameters materialParametersAt(const World& world, const Cell& cell) {
    if (const auto* material = world.tryGetConst<SurfaceMaterial>(
            cellAt(world, cell.x, cell.z)))
        return material->simulation;
    return SurfaceParameters{};
}

// Canonical Stage-3 update: climate, atmospheric state and material-dependent
// accumulation. Do NOT call advance/advanceClimate again in the same tick.
inline void advanceEnvironment(World& world) {
    advanceClimateWithSurface(world, [&](const Cell& cell) {
        return materialParametersAt(static_cast<const World&>(world), cell);
    });
}

inline bool environmentAt(const World& world, std::int32_t x,
                          std::int32_t z, EnvironmentalResponse& response) {
    Sample sampleValue{};
    if (!sample(world, x, z, sampleValue)) return false;
    const Entity entity = cellAt(world, x, z);
    const auto* material = world.tryGetConst<SurfaceMaterial>(entity);
    const auto permeability = material ?
        fxClamp(material->permeability, Fixed{}, Fixed::fromInt(1)) :
        Fixed::fromRatio(1, 2);
    const auto tractionDry = material ? material->tractionDry : Fixed::fromInt(1);
    response.temperature = sampleValue.atmosphere.temperature;
    response.humidity = sampleValue.atmosphere.humidity;
    response.wind = sampleValue.atmosphere.wind;
    response.rain = sampleValue.atmosphere.rain;
    response.snow = sampleValue.atmosphere.snow;
    response.wetness = sampleValue.surface.wetness;
    response.snowpack = sampleValue.surface.snowpack;
    response.ice = sampleValue.surface.ice;
    response.tractionMultiplier = fxClamp(tractionDry *
        (Fixed::fromInt(1) - response.wetness / 4 -
         response.ice * Fixed::fromRatio(3, 4) -
         fxClamp(response.snowpack, Fixed{}, Fixed::fromInt(1)) / 4),
         Fixed::fromRatio(1, 10), Fixed::fromInt(2));
    response.runoffPotential = fxClamp(response.rain *
        (Fixed::fromInt(1) - permeability), Fixed{}, Fixed::fromInt(1));
    response.fireSuppression = fxClamp(response.rain +
        response.wetness / 2, Fixed{}, Fixed::fromInt(1));
    return true;
}

} // namespace aiverse::physis
