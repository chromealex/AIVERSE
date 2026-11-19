// PHYSIS Stage 2: data-driven climate zones and migrating atmospheric fronts.
// All authoritative state resides in common ECS components; no rendering or genres.
#pragma once
#include "Physis.h"

namespace aiverse::physis {

struct ClimateZone {
    Fixed temperatureOffset{}; // constant regional delta in degrees C
    Fixed humidityOffset{};    // relative humidity delta [-1..1]
    Fixed pressureOffset{};    // hPa delta
    Vec2 prevailingWind{};     // m/s, x/z
    Fixed seasonTemperatureAmplitude = Fixed::fromInt(10);
    Fixed seasonHumidityAmplitude = Fixed::fromRatio(1, 10);
    std::uint32_t yearTicks = 24000; // full period in host's fixed ticks
    std::uint32_t phaseTicks = 0;    // biome offset, not wall time
};

struct WeatherFront {
    Vec2 center{};     // grid coordinates (can be fractional)
    Vec2 velocity{};   // grid cells per tick
    Fixed radiusCells = Fixed::fromInt(8); // Manhattan radius
    Fixed temperatureDelta{};
    Fixed humidityDelta{};
    Fixed pressureDelta{};
    Vec2 windDelta{};
    Fixed precipitationBoost{};
};

// Pure periodic triangle: winter=-1 at t=0, summer=+1 at t=yearTicks/2.
// No sin() and no floating point; stable across CPUs and replay.
inline Fixed seasonSignal(std::uint64_t tick, const ClimateZone& zone) {
    if (zone.yearTicks < 4) return Fixed{};
    const auto period = std::uint64_t(zone.yearTicks);
    const auto t = (tick + std::uint64_t(zone.phaseTicks)) % period;
    const auto half = period / 2;
    if (t <= half)
        return Fixed::fromInt(-1) + Fixed::fromRatio(std::int64_t(2 * t), std::int64_t(half));
    return Fixed::fromInt(1) -
        Fixed::fromRatio(std::int64_t(2 * (t - half)), std::int64_t(period - half));
}

inline Forcing forcingAt(const World& world, const Cell& cell, std::uint64_t tick) {
    Forcing result;
    const Entity cellEntity = cellAt(world, cell.x, cell.z);
    if (const auto* zone = world.tryGetConst<ClimateZone>(cellEntity)) {
        result.temperatureOffset = zone->temperatureOffset +
            zone->seasonTemperatureAmplitude * seasonSignal(tick, *zone);
        result.humidityOffset = zone->humidityOffset +
            zone->seasonHumidityAmplitude * seasonSignal(tick, *zone);
        result.pressureOffset = zone->pressureOffset;
        result.wind = zone->prevailingWind;
    }
    // Stable ECS iteration order. Front movement happens ONLY after all cells
    // see the same front positions in this tick.
    world.eachExistingConst<WeatherFront>([&](Entity, const WeatherFront& front) {
        if (front.radiusCells <= Fixed{}) return;
        const auto distance = fxAbs(Fixed::fromInt(cell.x) - front.center.x) +
                              fxAbs(Fixed::fromInt(cell.z) - front.center.y);
        const auto weight = fxClamp(Fixed::fromInt(1) - distance / front.radiusCells,
                                    Fixed{}, Fixed::fromInt(1));
        result.temperatureOffset += front.temperatureDelta * weight;
        result.humidityOffset += front.humidityDelta * weight;
        result.pressureOffset += front.pressureDelta * weight;
        result.wind += front.windDelta * weight;
        result.precipitationBoost += front.precipitationBoost * weight;
    });
    return result;
}

template<class SurfaceProvider, class ExtraForcingProvider>
inline void advanceClimateWithSurfaceAndForcing(World& world,
                                                 SurfaceProvider&& surfaceProvider,
                                                 ExtraForcingProvider&& extraForcing) {
    // Exactly one atmosphere/surface pass. Additional forcing is still pure
    // Fixed ECS data and cannot move weather fronts during cell iteration.
    advanceWith(world, [&](const Cell& cell, std::uint64_t tick) {
        Forcing base = forcingAt(static_cast<const World&>(world), cell, tick);
        const Forcing extra = extraForcing(cell, tick);
        base.temperatureOffset += extra.temperatureOffset;
        base.humidityOffset += extra.humidityOffset;
        base.pressureOffset += extra.pressureOffset;
        base.wind += extra.wind;
        base.precipitationBoost += extra.precipitationBoost;
        return base;
    }, surfaceProvider);
    world.eachExisting<WeatherFront>([](Entity, WeatherFront& front) {
        front.center += front.velocity;
    });
}

template<class SurfaceProvider>
inline void advanceClimateWithSurface(World& world, SurfaceProvider&& surfaceProvider) {
    advanceClimateWithSurfaceAndForcing(world, surfaceProvider,
        [](const Cell&, std::uint64_t) { return Forcing{}; });
}

inline void advanceClimate(World& world) {
    advanceClimateWithSurface(world, [](const Cell&) { return SurfaceParameters{}; });
}

} // namespace aiverse::physis
