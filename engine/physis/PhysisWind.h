// PHYSIS Stage 8: deterministic, genre-neutral 3D wind sampling (no renderer/Box3D).
// Wind is velocity in m/s; X/Z ground plane, Y up. Full ECS ownership.
#pragma once
#include "PhysisEnvironment.h"

namespace aiverse::physis {

// Local additive wind volume. A zero duration means it is active indefinitely.
// Gusts use quantized tick/sample cells and seeded integer mixing, never wall time.
struct WindZone {
    Vec3 center{};
    Fixed radiusMeters = Fixed::fromInt(100);
    Vec3 velocity{};                   // m/s
    Fixed gustAmplitude{};             // m/s, symmetric per axis
    std::uint64_t seed = 1;
    std::uint64_t startTick = 0;
    std::uint32_t durationTicks = 0;
    std::uint32_t gustPeriodTicks = 8;
};

// A simple isotropic shelter/obstacle modifier, not a CFD pressure solver.
// Only the queried wind vector is attenuated; authoritative weather is unchanged.
struct WindShelter {
    Vec3 center{};
    Fixed radiusMeters = Fixed::fromInt(20);
    Fixed attenuation = Fixed::fromRatio(1, 2); // 0 unchanged, 1 fully blocked
};

inline bool windActive(std::uint64_t tick, std::uint64_t start,
                       std::uint32_t duration) {
    return tick >= start && (duration == 0 || tick - start < duration);
}

// Fixed radial attenuation; explicitly avoid expensive sqrt outside the sphere.
inline Fixed radialWeight(Vec3 delta, Fixed radius) {
    if (radius <= Fixed{} ||
        fxAbs(delta.x) >= radius || fxAbs(delta.y) >= radius ||
        fxAbs(delta.z) >= radius) return Fixed{};
    // Normalize before squaring. Squaring world-distance coordinates in Q47.16
    // can overflow for a large but otherwise valid radius (e.g. 10 km+).
    const Vec3 normalized = delta / radius;
    const Fixed normalizedDistance = fxSqrt(normalized.lengthSq());
    return fxClamp(Fixed::fromInt(1) - normalizedDistance,
                   Fixed{}, Fixed::fromInt(1));
}

inline Fixed signedGust(std::uint64_t key) {
    const auto unit = std::int64_t(key & 1023ull);
    return Fixed::fromRatio(unit * 2 - 1023, 1023);
}

// Optional additional velocity from zones/gusts, without PHYSIS climate base.
template<class ZoneVisitor>
inline Vec3 localWindAtWith(const World& world, Vec3 position,
                            std::uint64_t tick, ZoneVisitor&& visitZones) {
    Vec3 velocity{};
    const auto* state = world.resourceConst<State>();
    const auto seed = state ? state->settings.seed : std::uint64_t(1);
    visitZones([&](Entity, const WindZone& zone) {
        if (!windActive(tick, zone.startTick, zone.durationTicks) ||
            zone.radiusMeters <= Fixed{}) return;
        const Fixed weight = radialWeight(position - zone.center, zone.radiusMeters);
        if (weight <= Fixed{}) return;
        velocity = velocity + zone.velocity * weight;
        if (zone.gustAmplitude <= Fixed{} || !zone.gustPeriodTicks) return;
        // Position affects turbulence independently of chunk / job ordering.
        const auto x = position.x.floorInt();
        const auto z = position.z.floorInt();
        const auto y = position.y.floorInt();
        const auto period = (tick - zone.startTick) / zone.gustPeriodTicks;
        const auto key = mix64(seed ^ zone.seed ^ mix64(static_cast<std::uint64_t>(x)) ^
                               mix64(static_cast<std::uint64_t>(z)) ^
                               mix64(static_cast<std::uint64_t>(y)) ^ mix64(period));
        const Fixed amplitude = fxClamp(zone.gustAmplitude,
                                       Fixed{}, Fixed::fromInt(100)) * weight;
        velocity = velocity + Vec3{
            signedGust(key) * amplitude,
            signedGust(key >> 10) * amplitude,
            signedGust(key >> 20) * amplitude
        };
    });
    return velocity;
}

inline Vec3 localWindAt(const World& world, Vec3 position, std::uint64_t tick) {
    return localWindAtWith(world, position, tick, [&](auto&& callback) {
        world.eachExistingConst<WindZone>(callback);
    });
}

// Canonical read-only wind field consumed by physics, foliage, particles,
// ballistics and camera/audio adapters. No consumer modifies ECS weather.
template<class ZoneVisitor, class ShelterVisitor>
inline Vec3 windAtWith(const World& world, Vec3 position,
                       ZoneVisitor&& visitZones, ShelterVisitor&& visitShelters) {
    const auto* state = world.resourceConst<State>();
    if (!state) return Vec3{};
    Vec3 velocity{};
    const Entity cell = cellAtPosition(world, position);
    if (const auto* atmosphere = world.tryGetConst<Atmosphere>(cell))
        velocity = {atmosphere->wind.x, Fixed{}, atmosphere->wind.y};
    velocity = velocity + localWindAtWith(world, position, state->tick, visitZones);
    visitShelters([&](Entity, const WindShelter& shelter) {
        if (shelter.radiusMeters <= Fixed{}) return;
        const Fixed factor = radialWeight(position - shelter.center,
                                          shelter.radiusMeters);
        const Fixed block = fxClamp(shelter.attenuation, Fixed{},
                                    Fixed::fromInt(1));
        velocity = velocity * (Fixed::fromInt(1) - block * factor);
    });
    const auto cap = Fixed::fromInt(200);
    velocity.x = fxClamp(velocity.x, -cap, cap);
    velocity.y = fxClamp(velocity.y, -cap, cap);
    velocity.z = fxClamp(velocity.z, -cap, cap);
    return velocity;
}

inline Vec3 windAt(const World& world, Vec3 position) {
    return windAtWith(world, position,
        [&](auto&& callback) { world.eachExistingConst<WindZone>(callback); },
        [&](auto&& callback) { world.eachExistingConst<WindShelter>(callback); });
}

} // namespace aiverse::physis
