// PHYSIS Stage 9: fixed-point gravity-field policy. No physics solver dependency.
// Gravity is acceleration in m/s^2, X/Z horizontal and Y up.
#pragma once
#include "PhysisWind.h"
#include <cstdint>

namespace aiverse::physis {

struct GravitySettings {
    // Standard earth baseline, independent of the optional Box3D default.
    Vec3 baseline{Fixed{}, Fixed::fromRatio(-981, 100), Fixed{}};
    Fixed maxAcceleration = Fixed::fromInt(200);
};

enum class GravityMode : std::uint8_t { Uniform = 0, Attract = 1, Repel = 2 };

struct GravityField {
    GravityMode mode = GravityMode::Uniform;
    Vec3 center{};
    Fixed radiusMeters{}; // zero => global for Uniform only
    Vec3 acceleration{};   // Uniform: m/s^2; radial modes use radialMagnitude
    Fixed radialMagnitude = Fixed::fromInt(10); // m/s^2
    std::int32_t priority = 0;
    bool overrideBaseline = false;  // e.g. moon/planet local field
    bool enabled = true;
};

// The sphere fades linearly to zero acceleration at its boundary.
// For a global Uniform field (radius=0), no fade is applied.
inline Fixed gravityWeight(const GravityField& field, Vec3 position) {
    if (!field.enabled) return Fixed{};
    if (field.mode == GravityMode::Uniform && field.radiusMeters == Fixed{})
        return Fixed::fromInt(1);
    return radialWeight(position - field.center, field.radiusMeters);
}

inline Vec3 gravityContribution(const GravityField& field,
                                Vec3 position, Fixed weight) {
    if (weight <= Fixed{}) return Vec3{};
    if (field.mode == GravityMode::Uniform)
        return field.acceleration * weight;
    if (field.mode != GravityMode::Attract && field.mode != GravityMode::Repel)
        return Vec3{};
    // Point gravity is a bounded authored field, not a Newtonian N-body
    // solver. Normalize by radius *before* squaring, avoiding overflow when
    // field/world coordinates span many kilometres.
    if (field.radiusMeters <= Fixed{}) return Vec3{};
    const Vec3 inward = (field.center - position) / field.radiusMeters;
    const Fixed distance = inward.length();
    if (distance <= Fixed{}) return Vec3{};
    const Fixed strength = fxClamp(field.radialMagnitude,
                                   Fixed{}, Fixed::fromInt(200)) * weight;
    const Vec3 direction = field.mode == GravityMode::Attract ?
        inward : inward * Fixed::fromInt(-1);
    return direction / distance * strength;
}

// Deterministic field selection: strongest override priority wins; ties are
// broken with the smallest generation-safe Entity ID (not ECS iteration order).
// Additive fields are superimposed in fixed-point arithmetic.
template<class FieldVisitor>
inline Vec3 gravityAtWith(const World& world, Vec3 position, FieldVisitor&& visitFields) {
    const GravitySettings defaults{};
    const auto* settings = world.resourceConst<GravitySettings>();
    const GravitySettings& config = settings ? *settings : defaults;
    Vec3 additive{};
    bool hasOverride = false;
    std::int32_t selectedPriority = 0;
    std::uint64_t selectedEntity = 0;
    Vec3 overrideAcceleration{};
    Fixed overrideWeight{};
    visitFields([&](Entity e, const GravityField& field) {
        if (!field.enabled) return;
        const Fixed weight = gravityWeight(field, position);
        if (weight <= Fixed{}) return;
        const Vec3 contribution = gravityContribution(field, position, weight);
        if (field.overrideBaseline) {
            if (!hasOverride || field.priority > selectedPriority ||
                (field.priority == selectedPriority && e.packed() < selectedEntity)) {
                hasOverride = true;
                selectedPriority = field.priority;
                selectedEntity = e.packed();
                overrideAcceleration = contribution;
                overrideWeight = weight;
            }
        } else {
            additive = additive + contribution;
        }
    });
    const Fixed remain = hasOverride ?
        Fixed::fromInt(1) - overrideWeight : Fixed::fromInt(1);
    Vec3 result = config.baseline * remain + (hasOverride ? overrideAcceleration : Vec3{}) + additive;
    const Fixed cap = fxClamp(config.maxAcceleration, Fixed::fromInt(1),
                               Fixed::fromInt(200));
    result.x = fxClamp(result.x, -cap, cap);
    result.y = fxClamp(result.y, -cap, cap);
    result.z = fxClamp(result.z, -cap, cap);
    return result;
}
inline Vec3 gravityAt(const World& world, Vec3 position) {
    return gravityAtWith(world, position,
        [&](auto&& callback) { world.eachExistingConst<GravityField>(callback); });
}

} // namespace aiverse::physis
