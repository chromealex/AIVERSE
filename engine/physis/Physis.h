// PHYSIS: genre-independent, authoritative environmental simulation (stage 1).
// Uses only the common AIVERSE ECS and deterministic Q47.16 Fixed arithmetic.
// A game or server drives advance() exactly once per committed fixed simulation tick.
#pragma once
#include "../core/ECS.h"
#include "../core/Fixed.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::physis {

// Grid cell coordinates are independent of any game genre, map renderer or camera.
struct Cell {
    std::int32_t x = 0;
    std::int32_t z = 0;
    Fixed elevation{};
};

struct Atmosphere {
    Fixed temperature = Fixed::fromInt(15);  // degrees Celsius
    Fixed humidity = Fixed::fromRatio(1, 2); // relative 0..1
    Fixed pressure = Fixed::fromInt(1013);   // hPa (reserved for later simulation)
    Vec2 wind{};                            // x/z metres per second
    Fixed rain{};                          // rate normalized 0..1
    Fixed snow{};                          // rate normalized 0..1
};

struct Surface {
    Fixed wetness{};   // normalized 0..1, authoritative; renderers consume it
    Fixed snowpack{};  // metres of snow water-equivalent (simplified accumulation)
    Fixed ice{};       // normalized 0..1; freezing surface coverage
};

struct Settings {
    std::int32_t originX = 0;   // grid coordinates of the first cell
    std::int32_t originZ = 0;
    std::int32_t width = 16;
    std::int32_t height = 16;
    std::int32_t cellSizeMeters = 128;
    std::uint64_t seed = 1;
    Fixed baseTemperature = Fixed::fromInt(15);
    Fixed baseHumidity = Fixed::fromRatio(1, 2);
};

// Both state and cell components live in aiverse::World and participate in
// World copying/COW snapshots. No hidden clocks, globals, random engines or
// persistent raw pointers. Persistent Simulation save is NOT wired up yet.
struct State {
    Settings settings{};
    std::uint64_t tick = 0;
    std::vector<Entity> cells; // row-major, z * width + x
};

struct Sample {
    Cell cell{};
    Atmosphere atmosphere{};
    Surface surface{};
};

// Stateless, portable 64-bit avalanche; never use std::hash for simulation.
inline std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

inline std::uint64_t cellKey(std::uint64_t seed, std::int32_t x, std::int32_t z) {
    return mix64(seed ^ (std::uint64_t(std::uint32_t(x)) << 32)
                      ^ std::uint64_t(std::uint32_t(z)));
}

inline std::int64_t floorDiv(std::int64_t numerator, std::int64_t denominator) {
    // denominator is positive (validated when initializing the world).
    const auto quotient = numerator / denominator;
    return numerator % denominator < 0 ? quotient - 1 : quotient;
}

inline void initialize(World& world, const Settings& config = {}) {
    if (world.resourceConst<State>()) {
        throw std::logic_error("PHYSIS already initialized in this World");
    }
    const std::int64_t count = std::int64_t(config.width) * config.height;
    if (config.width <= 0 || config.height <= 0 || count > 1048576 ||
        config.cellSizeMeters <= 0 ||
        std::int64_t(config.originX) + config.width > std::numeric_limits<std::int32_t>::max() ||
        std::int64_t(config.originZ) + config.height > std::numeric_limits<std::int32_t>::max() ||
        config.baseHumidity < Fixed{} || config.baseHumidity > Fixed::fromInt(1)) {
        throw std::invalid_argument("invalid PHYSIS grid or climate settings");
    }

    // Reserve before creating entities. Initialized cells are authoritative ECS data.
    State state;
    state.settings = config;
    state.cells.reserve(static_cast<std::size_t>(count));
    for (std::int32_t row = 0; row < config.height; ++row) {
        for (std::int32_t col = 0; col < config.width; ++col) {
            const auto x = config.originX + col;
            const auto z = config.originZ + row;
            Entity entity = world.create();
            world.add<Cell>(entity, Cell{x, z, {}});
            const std::uint64_t key = cellKey(config.seed, x, z);
            const auto temperatureNoise = std::int64_t(key & 1023ull) - 512;
            const auto humidityNoise = std::int64_t((key >> 10) & 1023ull) - 512;
            Atmosphere atmosphere;
            atmosphere.temperature = fxClamp(config.baseTemperature +
                Fixed::fromRaw(temperatureNoise * 64), Fixed::fromInt(-80), Fixed::fromInt(80));
            atmosphere.humidity = fxClamp(config.baseHumidity +
                Fixed::fromRaw(humidityNoise * 16), Fixed{}, Fixed::fromInt(1));
            world.add<Atmosphere>(entity, atmosphere);
            world.add<Surface>(entity, Surface{});
            state.cells.push_back(entity);
        }
    }
    world.setResource<State>(state);
}

inline Entity cellAt(const World& world, std::int32_t x, std::int32_t z) {
    const auto* state = world.resourceConst<State>();
    if (!state) return kNullEntity;
    const auto localX = std::int64_t(x) - state->settings.originX;
    const auto localZ = std::int64_t(z) - state->settings.originZ;
    if (localX < 0 || localZ < 0 ||
        localX >= state->settings.width || localZ >= state->settings.height)
        return kNullEntity;
    const auto index = static_cast<std::size_t>(localZ * state->settings.width + localX);
    const Entity entity = state->cells[index];
    return world.alive(entity) ? entity : kNullEntity;
}

inline Entity cellAtPosition(const World& world, Vec3 worldPosition) {
    const auto* state = world.resourceConst<State>();
    if (!state) return kNullEntity;
    const auto x = floorDiv(worldPosition.x.floorInt(), state->settings.cellSizeMeters);
    const auto z = floorDiv(worldPosition.z.floorInt(), state->settings.cellSizeMeters);
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max()) return kNullEntity;
    return cellAt(world, static_cast<std::int32_t>(x), static_cast<std::int32_t>(z));
}

inline bool read(const World& world, Entity entity, Sample& output) {
    const auto* position = world.tryGetConst<Cell>(entity);
    const auto* atmosphere = world.tryGetConst<Atmosphere>(entity);
    const auto* surface = world.tryGetConst<Surface>(entity);
    if (!position || !atmosphere || !surface) return false;
    output = Sample{*position, *atmosphere, *surface};
    return true;
}

inline bool sample(const World& world, std::int32_t x, std::int32_t z, Sample& output) {
    return read(world, cellAt(world, x, z), output);
}

// Stage-1 baseline evolution: localized seeded forcing, temperature / humidity
// relaxation, precipitation split and simple wetness/snow accumulation. This is
// NOT a physical weather-front solver, regional climate or fluid simulation.
// Optional deterministic forcing from independent climate / environmental modules.
struct Forcing {
    Fixed temperatureOffset{};
    Fixed humidityOffset{};
    Fixed pressureOffset{};
    Vec2 wind{};
    Fixed precipitationBoost{};
};

// Opt-in material response coefficients. Default values preserve the Stage-1
// rain/snow accumulation rates. All coefficients are per fixed simulation tick.
struct SurfaceParameters {
    Fixed wettingRate = Fixed::fromRatio(1, 16);
    Fixed dryingRate = Fixed::fromRatio(1, 256);
    Fixed snowAccumulationRate = Fixed::fromRatio(1, 128);
    Fixed meltRate = Fixed::fromRatio(1, 128);
    Fixed freezingRate = Fixed::fromRatio(1, 32);
    Fixed thawingRate = Fixed::fromRatio(1, 32);
    Fixed freezingPoint{};
};

// Standalone system pass: call only after atmosphere and before tick increment.
// Hosts that bypass advanceWith() may call this once with their own scheduling.
template<class SurfaceProvider>
inline void advanceSurface(World& world, SurfaceProvider&& getParameters) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    for (const Entity entity : state->cells) {
        if (!world.alive(entity)) continue;
        const auto* coord = world.tryGetConst<Cell>(entity);
        const auto* atmosphere = world.tryGetConst<Atmosphere>(entity);
        if (!coord || !atmosphere) continue;
        auto* surface = world.tryGet<Surface>(entity);
        if (!surface) continue;
        const SurfaceParameters parameters = getParameters(*coord);
        surface->wetness = fxClamp(
            surface->wetness + atmosphere->rain * parameters.wettingRate -
            parameters.dryingRate, Fixed{}, Fixed::fromInt(1));
        const bool frozen = atmosphere->temperature <= parameters.freezingPoint;
        surface->snowpack = fxClamp(
            surface->snowpack + atmosphere->snow * parameters.snowAccumulationRate -
            (frozen ? Fixed{} : parameters.meltRate),
            Fixed{}, Fixed::fromInt(10));
        surface->ice = fxClamp(
            surface->ice + (frozen ? surface->wetness * parameters.freezingRate :
                            -parameters.thawingRate),
            Fixed{}, Fixed::fromInt(1));
    }
}

// A single deterministic scheduler entry point, independently extendable by
// any data-only forcing provider. Each grid cell is written exactly once per pass.
template<class ForcingProvider, class SurfaceProvider>
inline void advanceWith(World& world, ForcingProvider&& getForcing,
                        SurfaceProvider&& getSurfaceParameters) {
    auto* state = world.resource<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    if (state->tick == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("PHYSIS tick overflow");
    const auto seed = state->settings.seed;
    const auto tick = state->tick;
    const auto baselineTemperature = state->settings.baseTemperature;
    const auto baselineHumidity = state->settings.baseHumidity;
    for (const Entity entity : state->cells) {
        if (!world.alive(entity)) continue;
        const auto* coord = world.tryGetConst<Cell>(entity);
        if (!coord) continue;
        auto* atmosphere = world.tryGet<Atmosphere>(entity);
        if (!atmosphere) continue;
        const Forcing forcing = getForcing(*coord, tick);
        const auto key = mix64(cellKey(seed, coord->x, coord->z) ^
                               mix64(tick + 0x6a09e667f3bcc909ull));
        const auto a = std::int64_t(key & 1023ull) - 512;
        const auto b = std::int64_t((key >> 10) & 1023ull) - 512;
        const auto c = std::int64_t((key >> 20) & 1023ull) - 512;
        atmosphere->temperature = fxClamp(
            atmosphere->temperature + (baselineTemperature + forcing.temperatureOffset -
                 atmosphere->temperature) / 64 +
                Fixed::fromRaw(a * 8), Fixed::fromInt(-80), Fixed::fromInt(80));
        atmosphere->humidity = fxClamp(
            atmosphere->humidity + (fxClamp(baselineHumidity + forcing.humidityOffset,
                 Fixed{}, Fixed::fromInt(1)) - atmosphere->humidity) / 32 +
                Fixed::fromRaw(b * 4), Fixed{}, Fixed::fromInt(1));
        atmosphere->wind.x = fxClamp(
            atmosphere->wind.x + (forcing.wind.x - atmosphere->wind.x) / 32 +
                Fixed::fromRaw(b * 32),
            Fixed::fromInt(-30), Fixed::fromInt(30));
        atmosphere->wind.y = fxClamp(
            atmosphere->wind.y + (forcing.wind.y - atmosphere->wind.y) / 32 +
                Fixed::fromRaw(c * 32),
            Fixed::fromInt(-30), Fixed::fromInt(30));
        atmosphere->pressure = fxClamp(
            atmosphere->pressure + (Fixed::fromInt(1013) + forcing.pressureOffset -
                atmosphere->pressure) / 64,
            Fixed::fromInt(800), Fixed::fromInt(1200));
        const auto precipitation = fxClamp(
            (atmosphere->humidity - Fixed::fromRatio(3, 4)) * 4 + forcing.precipitationBoost,
            Fixed{}, Fixed::fromInt(1));
        atmosphere->rain = atmosphere->temperature > Fixed{} ? precipitation : Fixed{};
        atmosphere->snow = atmosphere->temperature <= Fixed{} ? precipitation : Fixed{};
    }
    advanceSurface(world, getSurfaceParameters);
    ++state->tick;
}

// Source-compatible default surface materials.
template<class ForcingProvider>
inline void advanceWith(World& world, ForcingProvider&& getForcing) {
    advanceWith(world, getForcing, [](const Cell&) { return SurfaceParameters{}; });
}

// Simple seeded environment: no forcing source or genre-specific behavior.
inline void advance(World& world) {
    advanceWith(world, [](const Cell&, std::uint64_t) { return Forcing{}; });
}

} // namespace aiverse::physis
