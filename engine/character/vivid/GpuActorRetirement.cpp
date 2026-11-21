#include "GpuActorRetirement.h"

#include <utility>

namespace aiverse::vivid {

bool GpuActorRetirementQueue::retire(Resource& actor) {
    if (!actor || actor->device_ != &device_ || !actor->ready())
        return false;
    // Generic queue reserves pending storage before moving this actor.
    // If allocation throws, caller still holds ownership.
    queue_.enqueueFrom(actor);
    return true;
}

void GpuActorRetirementQueue::release(Resource& actor) noexcept {
    if (!actor) return;
    // Called only after a confirmed token or full idle barrier. Never
    // destroy GPU buffers based on frame counts or elapsed real time.
    actor->releaseAfterGpuCompletion();
    actor.reset();
}

bool GpuActorRetirementQueue::collect() {
    return queue_.collect(
        [&]() { return device_.beginResourceRetirement(); },
        [&](std::uint64_t token) {
            return device_.resourceRetirementComplete(token);
        },
        [&](std::uint64_t token) {
            if (device_.resourceRetirementStatus(token) ==
                gfx::ResourceRetirementStatus::Complete)
                device_.endResourceRetirement(token);
            else
                device_.abandonRetirementTokenAfterIdle(token);
        },
        [&]() { return device_.waitForResourceIdle(); },
        [&](Resource& actor) { release(actor); },
        [&](std::uint64_t token) {
            return device_.resourceRetirementStatus(token) ==
                   gfx::ResourceRetirementStatus::Failed;
        });
}

bool GpuActorRetirementQueue::drain() {
    return queue_.drain(
        [&]() { return device_.waitForResourceIdle(); },
        [&](std::uint64_t token) {
            device_.abandonRetirementTokenAfterIdle(token);
        },
        [&](Resource& actor) { release(actor); });
}

} // namespace aiverse::vivid
