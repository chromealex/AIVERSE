// PHYSIS Stage 4: optional deterministic wildfire / shallow flood / quake layer.
// Generic spatial ECS state. No genre, renderer, or platform dependencies.
#pragma once
#include "PhysisEnvironment.h"
#include <array>
#include <algorithm>

namespace aiverse::physis {

struct HazardCell {
    Fixed fuel = Fixed::fromInt(1); // remaining flammable fuel, normalized
    Fixed fire{};                   // local fire intensity, normalized
    Fixed waterDepth{};             // metres above terrain (shallow approximation)
    Fixed quake{};                  // instantaneous normalized ground-shaking intensity
};

// Select exactly one water authority per world. When NEREID (or any external
// water service) is authoritative, PHYSIS retains fire/quake but does not
// advance its standalone shallow-water approximation.
enum class FloodMode : std::uint8_t { LocalShallow = 0, ExternalAuthority = 1 };

struct DisasterConfig {
    Fixed fireSpreadRate = Fixed::fromRatio(1, 16);
    Fixed fireGrowthRate = Fixed::fromRatio(1, 64);
    Fixed fuelConsumptionRate = Fixed::fromRatio(1, 128);
    Fixed rainSuppressionRate = Fixed::fromRatio(1, 8);
    Fixed wetnessSuppressionRate = Fixed::fromRatio(1, 64);
    Fixed rainInflowRate = Fixed::fromRatio(1, 64); // metres per normalized rain tick
    Fixed waterFlowFraction = Fixed::fromRatio(1, 4);
    Fixed evaporationRate{}; // metres / tick; explicit zero by default
};

// Disasters own their fixed-tick guard in a world resource for rollback.
struct DisasterState {
    DisasterConfig config{};
    std::uint64_t lastProcessedTick = 0;
};

// A generic timed, spatial event. Consumers decide whether to shake a camera,
// stress a building or produce audiovisual FX; PHYSIS doesn't access them.
struct QuakeEvent {
    std::int32_t centerX = 0;
    std::int32_t centerZ = 0;
    Fixed radiusCells = Fixed::fromInt(8);
    Fixed magnitude = Fixed::fromInt(1);
    std::uint64_t startTick = 1;
    std::uint32_t durationTicks = 30;
};

inline void initializeDisasters(World& world, DisasterConfig config = {}) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS required before disasters");
    if (world.resourceConst<DisasterState>())
        throw std::logic_error("PHYSIS disasters already initialized");
    if (config.fireSpreadRate < Fixed{} || config.fireGrowthRate < Fixed{} ||
        config.fuelConsumptionRate < Fixed{} || config.rainSuppressionRate < Fixed{} ||
        config.wetnessSuppressionRate < Fixed{} || config.rainInflowRate < Fixed{} ||
        config.waterFlowFraction < Fixed{} ||
        config.waterFlowFraction > Fixed::fromInt(1) ||
        config.evaporationRate < Fixed{})
        throw std::invalid_argument("negative/invalid PHYSIS disaster coefficients");
    for (Entity entity : state->cells)
        if (world.alive(entity) && !world.has<HazardCell>(entity))
            world.add<HazardCell>(entity, HazardCell{});
    world.setResource<DisasterState>(DisasterState{config, state->tick});
}

inline bool ignite(World& world, std::int32_t x, std::int32_t z,
                   Fixed strength = Fixed::fromInt(1)) {
    if (!world.resourceConst<DisasterState>()) return false;
    auto* hazard = world.tryGet<HazardCell>(cellAt(world, x, z));
    if (!hazard || hazard->fuel <= Fixed{}) return false;
    hazard->fire = fxClamp(fxMax(hazard->fire, strength), Fixed{}, Fixed::fromInt(1));
    return true;
}

inline bool hazardAt(const World& world, std::int32_t x, std::int32_t z,
                     HazardCell& output) {
    const auto* hazard = world.tryGetConst<HazardCell>(cellAt(world, x, z));
    if (!hazard) return false;
    output = *hazard;
    return true;
}

inline void advanceDisasters(World& world,
                             FloodMode floodMode = FloodMode::LocalShallow) {
    if (floodMode != FloodMode::LocalShallow && floodMode != FloodMode::ExternalAuthority)
        throw std::invalid_argument("invalid PHYSIS flood authority");
    const auto* climate = world.resourceConst<State>();
    auto* disaster = world.resource<DisasterState>();
    if (!climate || !disaster) throw std::logic_error("PHYSIS disasters not initialized");
    // The atmosphere / surface update must have committed exactly one tick.
    if (climate->tick != disaster->lastProcessedTick + 1)
        throw std::logic_error("PHYSIS disaster step must follow one climate tick");
    const auto config = disaster->config;
    const std::int32_t width = climate->settings.width;
    const std::int32_t height = climate->settings.height;
    const std::size_t count = climate->cells.size();
    std::vector<HazardCell> old(count);
    std::vector<HazardCell> next(count);
    std::vector<std::int64_t> waterDeltas(count, 0);
    std::vector<Cell> positions(count);
    std::vector<Atmosphere> air(count);
    std::vector<Surface> surfaces(count);

    // Read phase: snapshot each component once; subsequent work cannot observe
    // writes from this same disaster step.
    for (std::size_t i = 0; i < count; ++i) {
        const Entity entity = climate->cells[i];
        if (const auto* value = world.tryGetConst<HazardCell>(entity)) old[i] = *value;
        if (const auto* value = world.tryGetConst<Cell>(entity)) positions[i] = *value;
        if (const auto* value = world.tryGetConst<Atmosphere>(entity)) air[i] = *value;
        if (const auto* value = world.tryGetConst<Surface>(entity)) surfaces[i] = *value;
        next[i] = old[i];
    }
    const std::array<std::int32_t, 4> dx{1, -1, 0, 0};
    const std::array<std::int32_t, 4> dz{0, 0, 1, -1};
    for (std::int32_t z = 0; z < height; ++z) {
        for (std::int32_t x = 0; x < width; ++x) {
            const auto i = std::size_t(z) * std::size_t(width) + std::size_t(x);
            const auto& prior = old[i];
            auto& result = next[i];
            Fixed neighborFire{};
            const auto head = positions[i].elevation + prior.waterDepth;
            std::array<std::size_t, 4> drains{};
            std::array<std::int64_t, 4> gradients{};
            std::size_t drainCount = 0;
            std::int64_t totalGradient = 0;
            for (std::size_t direction = 0; direction < 4; ++direction) {
                const auto nx = x + dx[direction], nz = z + dz[direction];
                if (nx < 0 || nz < 0 || nx >= width || nz >= height) continue;
                const auto j = std::size_t(nz) * std::size_t(width) + std::size_t(nx);
                // Wind in the direction of spread increases ember transport.
                const auto wind = direction < 2 ? air[i].wind.x : air[i].wind.y;
                const auto downwind = (direction % 2 == 0) ? wind : -wind;
                const auto windGain = fxClamp(downwind / 20, Fixed{}, Fixed::fromInt(1));
                neighborFire += old[j].fire * (Fixed::fromInt(1) + windGain);
                if (floodMode == FloodMode::LocalShallow) {
                    const auto neighborHead = positions[j].elevation + old[j].waterDepth;
                    const auto gradient = (head - neighborHead).raw;
                    if (gradient > 0) {
                        drains[drainCount] = j;
                        gradients[drainCount] = gradient;
                        totalGradient += gradient;
                        ++drainCount;
                    }
                }
            }
            const auto rainSuppression = air[i].rain * config.rainSuppressionRate;
            const auto wetSuppression = surfaces[i].wetness * config.wetnessSuppressionRate;
            const auto seededVariation = Fixed::fromRatio(
                std::int64_t(mix64(cellKey(climate->settings.seed,
                    positions[i].x, positions[i].z) ^ climate->tick) & 255ull), 1024);
            result.fire = fxClamp(prior.fire +
                neighborFire * config.fireSpreadRate *
                (Fixed::fromInt(1) + seededVariation) +
                prior.fire * config.fireGrowthRate -
                rainSuppression - wetSuppression, Fixed{}, Fixed::fromInt(1));
            result.fuel = fxClamp(prior.fuel - prior.fire * config.fuelConsumptionRate,
                                  Fixed{}, Fixed::fromInt(1));
            if (result.fuel <= Fixed{}) result.fire = Fixed{};

            // Each source has a bounded per-tick outflow. Apply opposite signed
            // integer-raw deltas to source and destination for exact conservation
            // in the absence of rain, evaporation and depth clamping.
            if (floodMode == FloodMode::LocalShallow &&
                drainCount > 0 && prior.waterDepth > Fixed{} && totalGradient > 0) {
                std::int64_t outgoing = (prior.waterDepth *
                    config.waterFlowFraction).raw;
                outgoing = std::min(outgoing, prior.waterDepth.raw);
                std::int64_t remainder = outgoing;
                for (std::size_t k = 0; k < drainCount; ++k) {
                    const std::int64_t share = (k + 1 == drainCount)
                        ? remainder : outgoing * gradients[k] / totalGradient;
                    waterDeltas[i] -= share;
                    waterDeltas[drains[k]] += share;
                    remainder -= share;
                }
            }
            result.quake = Fixed{};
            world.eachExistingConst<QuakeEvent>([&](Entity, const QuakeEvent& event) {
                if (event.radiusCells <= Fixed{} ||
                    climate->tick < event.startTick ||
                    climate->tick - event.startTick >= event.durationTicks)
                    return;
                const auto distance =
                    fxAbs(Fixed::fromInt(positions[i].x) - Fixed::fromInt(event.centerX)) +
                    fxAbs(Fixed::fromInt(positions[i].z) - Fixed::fromInt(event.centerZ));
                const auto falloff = fxClamp(Fixed::fromInt(1) -
                    distance / event.radiusCells, Fixed{}, Fixed::fromInt(1));
                result.quake = fxMax(result.quake,
                    fxClamp(event.magnitude * falloff, Fixed{}, Fixed::fromInt(1)));
            });
        }
    }
    // Commit phase. Read-only clients never see any partially propagated fire or
    // water depth during the computation of the next disaster state.
    for (std::size_t i = 0; i < count; ++i) {
        auto* hazard = world.tryGet<HazardCell>(climate->cells[i]);
        if (!hazard) continue;
        // ExternalAuthority explicitly clears the legacy local flood cache.
        // NEREID owns water level/depth via its own ECS; never mirror it here.
        next[i].waterDepth = floodMode == FloodMode::LocalShallow ? fxClamp(
            old[i].waterDepth + Fixed::fromRaw(waterDeltas[i]) +
            air[i].rain * config.rainInflowRate - config.evaporationRate,
            Fixed{}, Fixed::fromInt(100)) : Fixed{};
        *hazard = next[i];
    }
    disaster->lastProcessedTick = climate->tick;
}

// Canonical opt-in scheduler for environments with disasters. Exactly one
// climate step and one disaster step; native projects may keep either separate.
inline void advanceEnvironmentWithDisasters(
    World& world, FloodMode floodMode = FloodMode::LocalShallow) {
    // Reject invalid host configuration before advancing authoritative climate.
    if (floodMode != FloodMode::LocalShallow && floodMode != FloodMode::ExternalAuthority)
        throw std::invalid_argument("invalid PHYSIS flood authority");
    advanceEnvironment(world);
    advanceDisasters(world, floodMode);
}

} // namespace aiverse::physis
