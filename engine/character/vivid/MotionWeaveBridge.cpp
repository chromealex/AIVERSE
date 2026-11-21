#include "MotionWeaveBridge.h"

#include <cmath>
#include <memory>
#include <utility>

namespace aiverse::vivid {

bool composeFaceAfterMotionWeave(
    const motionweave::AnimationPoseComponent& source,
    const FaceFrame& facial,
    const MorphBinding& binding,
    render::Pose& output,
    render::Pose& scratch,
    float influence,
    MorphApply mode) {
    if (!source.valid || source.local.size() != binding.nodes() ||
        !std::isfinite(influence) || influence < 0.0f || influence > 1.0f ||
        std::addressof(output) == std::addressof(scratch) ||
        std::addressof(output) == std::addressof(source.local) ||
        std::addressof(scratch) == std::addressof(source.local))
        return false;

    // Do not modify source.local: MotionWeave may retain it when animation
    // budget/visibility throttles the next pose evaluation.
    scratch = source.local;
    if (!binding.apply(facial, scratch, influence, mode))
        return false;

    // Swap ownership of preallocated storage instead of copying a second time.
    output.swap(scratch);
    return true;
}

} // namespace aiverse::vivid
