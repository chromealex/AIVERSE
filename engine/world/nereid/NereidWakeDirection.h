// NEREID — optional deterministic wake motion metadata.
// Separate persisted component: preserves v1 WaterInteractionOutput schema.
// Renderer/SpectraFX consume snapshots; no genre or physics integration.
#pragma once
#include "NereidWater.h"

namespace aiverse::lib::nereid {

struct WaterWakeDirectionComponent {
    Vec2 direction{};           // normalized X/Z movement; zero if stationary
    Fixed travelled{};          // distance since prior valid probe sample
    uint32_t sampledTick=0;
};

// Fixed-point movement policy, pure and without floating point.
inline WaterWakeDirectionComponent waterWakeDirection(
        Vec3 from,Vec3 to,uint32_t tick) {
    WaterWakeDirectionComponent result;
    result.sampledTick=tick;
    const Vec2 d{to.x-from.x,to.z-from.z};
    result.travelled=d.length();
    if(result.travelled>Fixed{})
        result.direction=d/result.travelled;
    return result;
}
inline void io(Archive& archive,WaterWakeDirectionComponent& value) {
    archive(value.direction.x,value.direction.y,
            value.travelled,value.sampledTick);
}
} // namespace aiverse::lib::nereid
