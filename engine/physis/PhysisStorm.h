// PHYSIS Stage 10: storms as deterministic ECS events and atmospheric forcing.
// Simplified authored vortex/cyclone model; not a mesoscale CFD weather solver.
#pragma once
#include "PhysisGravity.h"
#include "PhysisDisasters.h"
#include <cstdint>

namespace aiverse::physis {

enum class StormKind : std::uint8_t { Hurricane = 0, Tornado = 1 };

struct StormEvent {
    StormKind kind = StormKind::Hurricane;
    Vec3 center{};                   // Fixed world metres
    Vec3 driftPerTick{};             // Fixed metres per committed climate tick
    Fixed eyeRadiusMeters = Fixed::fromInt(200);
    Fixed outerRadiusMeters = Fixed::fromInt(2000);
    Fixed heightMeters = Fixed::fromInt(5000);
    Fixed tangentialWind = Fixed::fromInt(35); // m/s at peak ring
    Fixed radialInflow = Fixed::fromInt(2);    // m/s toward core
    Fixed updraft = Fixed::fromInt(5);        // m/s in core
    Fixed precipitationBoost = Fixed::fromRatio(1, 3); // normalized 0..1
    Fixed pressureDrop = Fixed::fromInt(30);  // positive hPa decrease
    std::uint64_t startTick = 0;
    std::uint32_t durationTicks = 0;         // zero = indefinite
    bool clockwise = false;
    bool enabled = true;
};

struct StormInfluence {
    Vec3 wind{};                // physical velocity: X/Z wind, Y updraft
    Fixed precipitationBoost{};
    Fixed pressureOffset{};
};

inline bool stormActive(const StormEvent& event, std::uint64_t tick) {
    return event.enabled && windActive(tick, event.startTick,
                                       event.durationTicks);
}

inline StormInfluence stormContribution(const StormEvent& event,
                                         Vec3 position,
                                         std::uint64_t tick) {
    StormInfluence out;
    if (!stormActive(event, tick) ||
        event.eyeRadiusMeters <= Fixed{} ||
        event.outerRadiusMeters <= event.eyeRadiusMeters ||
        event.heightMeters <= Fixed{}) return out;
    const Vec3 delta = position - event.center;
    const Fixed ax = fxAbs(delta.x), az = fxAbs(delta.z);
    if (ax >= event.outerRadiusMeters || az >= event.outerRadiusMeters ||
        fxAbs(delta.y) >= event.heightMeters) return out;
    // Normalize before squaring to avoid Q47.16 overflow for wide hurricanes.
    const Fixed normX = delta.x / event.outerRadiusMeters;
    const Fixed normZ = delta.z / event.outerRadiusMeters;
    const Fixed normalizedDistance = fxSqrt(normX * normX + normZ * normZ);
    if (normalizedDistance >= Fixed::fromInt(1)) return out;
    const Fixed distance = normalizedDistance * event.outerRadiusMeters;
    const Fixed vertical = Fixed::fromInt(1) -
        fxAbs(delta.y) / event.heightMeters;
    const Fixed edgeWeight = fxClamp((event.outerRadiusMeters - distance) /
                                      (event.outerRadiusMeters - event.eyeRadiusMeters),
                                      Fixed{}, Fixed::fromInt(1));
    const Fixed ringWeight = distance < event.eyeRadiusMeters ?
        distance / event.eyeRadiusMeters : edgeWeight;
    const Fixed weatherWeight = fxClamp(Fixed::fromInt(1) -
                                      distance / event.outerRadiusMeters,
                                      Fixed{}, Fixed::fromInt(1)) * vertical;
    out.precipitationBoost = fxClamp(event.precipitationBoost, Fixed{},
                                     Fixed::fromInt(1)) * weatherWeight;
    out.pressureOffset = -fxClamp(event.pressureDrop, Fixed{},
                                  Fixed::fromInt(150)) * weatherWeight;
    if (distance == Fixed{}) {
        out.wind.y = fxClamp(event.updraft, Fixed{}, Fixed::fromInt(150)) * vertical;
        return out;
    }
    const Vec2 radial = Vec2{delta.x, delta.z} / distance;
    const Fixed tangential = fxClamp(event.tangentialWind, Fixed{},
                                     Fixed::fromInt(200)) * ringWeight * vertical;
    const Fixed inflow = fxClamp(event.radialInflow, Fixed{},
                                 Fixed::fromInt(100)) * weatherWeight;
    // Counter-clockwise around +Y by default; clockwise reverses swirl.
    const Fixed direction = event.clockwise ? Fixed::fromInt(-1) : Fixed::fromInt(1);
    out.wind.x = -radial.y * tangential * direction - radial.x * inflow;
    out.wind.z = radial.x * tangential * direction - radial.y * inflow;
    out.wind.y = fxClamp(event.updraft, Fixed{}, Fixed::fromInt(150)) *
                 (event.kind == StormKind::Tornado ? edgeWeight : weatherWeight) *
                 vertical;
    return out;
}

// Read-only physical wind contribution; storms do not modify WindZone or physics.
template<class StormVisitor>
inline StormInfluence stormsAtWith(const World& world, Vec3 position,
                                   std::uint64_t tick, StormVisitor&& visitStorms) {
    (void)world; // visitor supplies candidates; keeping World mirrors reference API.
    StormInfluence total;
    visitStorms([&](Entity, const StormEvent& event) {
        const auto influence = stormContribution(event, position, tick);
        total.wind = total.wind + influence.wind;
        total.precipitationBoost += influence.precipitationBoost;
        total.pressureOffset += influence.pressureOffset;
    });
    const auto maxSpeed = Fixed::fromInt(200);
    total.wind.x = fxClamp(total.wind.x, -maxSpeed, maxSpeed);
    total.wind.y = fxClamp(total.wind.y, -maxSpeed, maxSpeed);
    total.wind.z = fxClamp(total.wind.z, -maxSpeed, maxSpeed);
    total.precipitationBoost = fxClamp(total.precipitationBoost,
                                       Fixed{}, Fixed::fromInt(1));
    total.pressureOffset = fxClamp(total.pressureOffset,
                                   Fixed::fromInt(-150), Fixed{});
    return total;
}

inline StormInfluence stormsAt(const World& world, Vec3 position,
                               std::uint64_t tick) {
    return stormsAtWith(world, position, tick,
        [&](auto&& callback) { world.eachExistingConst<StormEvent>(callback); });
}

inline Vec3 windWithStormsAt(const World& world, Vec3 position) {
    const auto* state = world.resourceConst<State>();
    const Vec3 base = windAt(world, position);
    if (!state) return base;
    const Vec3 storm = stormsAt(world, position, state->tick).wind;
    const Fixed cap = Fixed::fromInt(200);
    return {fxClamp(base.x + storm.x, -cap, cap),
            fxClamp(base.y + storm.y, -cap, cap),
            fxClamp(base.z + storm.z, -cap, cap)};
}

inline Forcing stormForcingAt(const World& world,
                              const Cell& cell, std::uint64_t tick) {
    Forcing result{};
    const auto* state = world.resourceConst<State>();
    if (!state) return result;
    const auto side = std::int64_t(state->settings.cellSizeMeters);
    const auto mx = std::int64_t(cell.x) * side + side / 2;
    const auto mz = std::int64_t(cell.z) * side + side / 2;
    // Fixed::fromInt converts to raw; prevent signed overflow on invalid maps.
    constexpr std::int64_t kMaxMetres =
        (std::numeric_limits<std::int64_t>::max() / Fixed::kOne) / 2;
    if (mx < -kMaxMetres || mx > kMaxMetres ||
        mz < -kMaxMetres || mz > kMaxMetres) return result;
    const Vec3 position{Fixed::fromInt(mx), cell.elevation, Fixed::fromInt(mz)};
    const auto influence = stormsAt(world, position, tick);
    result.pressureOffset = influence.pressureOffset;
    // Climate's precipitation formula multiplies the humidity excess by 4;
    // normalize authored storm intensity into that same forcing scale.
    result.precipitationBoost = influence.precipitationBoost * 4;
    // Physical storm wind is added by windWithStormsAt, not also to the
    // slowly relaxing climate wind. Avoid double-applying storm velocity.
    return result;
}

// Moves tracks once after climate has consumed their previous-tick positions.
inline void advanceStormTracks(World& world, std::uint64_t processedTick) {
    world.eachExisting<StormEvent>([&](Entity, StormEvent& event) {
        if (stormActive(event, processedTick))
            event.center = event.center + event.driftPerTick;
    });
}

// Single climate tick, including pressure/rain from storms, no physics step.
inline void advanceEnvironmentWithStorms(World& world) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    const auto tick = state->tick;
    advanceClimateWithSurfaceAndForcing(world,
        [&](const Cell& cell) {
            return materialParametersAt(static_cast<const World&>(world), cell);
        },
        [&](const Cell& cell, std::uint64_t time) {
            return stormForcingAt(static_cast<const World&>(world), cell, time);
        });
    advanceStormTracks(world, tick);
}

inline void advanceEnvironmentWithStormsAndDisasters(
    World& world, FloodMode mode = FloodMode::LocalShallow) {
    if (mode != FloodMode::LocalShallow && mode != FloodMode::ExternalAuthority)
        throw std::invalid_argument("invalid PHYSIS flood authority");
    advanceEnvironmentWithStorms(world);
    advanceDisasters(world, mode);
}

} // namespace aiverse::physis
