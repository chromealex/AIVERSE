// Shared, deterministic fixed-point regeneration primitive.
// Pure policy: no genre, ECS storage, game module or rendering dependency.
// The caller owns current value and provides a validated cap/rate/timestep.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/core/Fixed.h"

namespace aiverse::lib::mechanics {

// Preserve the exact Fixed arithmetic order used by existing RTS Unit
// health/energy regeneration: current + (rate * dt), clamped to maximum.
// In particular negative rates and negative current are not silently changed.
inline Fixed stepFixedResourceRegeneration(Fixed current,Fixed maximum,
                                            Fixed ratePerSecond,Fixed dt) {
    return fxMin(maximum,current+ratePerSecond*dt);
}

} // namespace aiverse::lib::mechanics
