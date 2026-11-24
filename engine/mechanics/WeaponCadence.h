// Fixed-point sub-tick cadence policy. No genre or ECS ownership dependency.
#pragma once
#include "engine/core/Fixed.h"
#include <cstdint>

namespace aiverse::lib::mechanics {
inline constexpr uint32_t kMaxCadenceShotsPerTick = 32;

// A ready weapon has no accumulated credit. Only crossing zero from an
// outstanding cooldown earns the remainder of this simulation tick.
inline Fixed advanceWeaponCadence(Fixed remaining, Fixed elapsed, bool precise) {
    if (remaining.raw <= 0) return Fixed{};
    const Fixed next = remaining - fxMax(Fixed{}, elapsed);
    return precise ? next : fxMax(Fixed{}, next);
}
inline bool cadenceReady(Fixed remaining) { return remaining.raw <= 0; }
// Only call after the owner accepts a shot (ammo, target, charge, etc.).
inline Fixed acceptCadenceShot(Fixed remaining, Fixed period, bool precise) {
    return precise ? remaining + period : period;
}
// Discard unused credit at the tick boundary (blocked, idle or budget exhausted).
inline Fixed finishWeaponCadence(Fixed remaining) { return fxMax(Fixed{}, remaining); }
} // namespace aiverse::lib::mechanics
