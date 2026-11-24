// MotionWeave — AIVERSE's modular animation system.
// Public, stable entry point for native game modules. Core animation types
// remain in aiverse::lib::animation for backwards compatibility.
// MotionWeave's presentation World is separate from the authoritative
// Simulation::world(), which must remain deterministic.
#pragma once
#include "EcsAnimationSystems.h"
#include "MotionWeaveScheduler.h"

namespace aiverse::lib::animation {

// Canonical branded entry for the shared ECS animation scheduler.
// Further capabilities (Retarget, Montage, IK, Ragdoll, Motion Warping,
// GPU Crowd) are optional modules under <aiverselib/animation/...>.
struct MotionWeave {
    static void update(World& presentation,float dt) {
        stepAnimationPresentation(presentation,dt);
    }
    // Host-owned scheduler enables persistent multithreading. The original
    // two-argument overload remains sequential and backwards-compatible.
    static MotionWeaveScheduleStats update(
            World& presentation,float dt,MotionWeaveScheduler& scheduler) {
        return scheduler.update(presentation,dt);
    }
};

} // namespace aiverse::lib::animation

namespace aiverse {
// New preferred namespace. Existing lib::animation code remains source-compatible.
namespace motionweave = ::aiverse::lib::animation;
}
namespace aiverse::lib {
namespace motionweave = animation;
}
