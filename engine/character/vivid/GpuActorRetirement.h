// VIVID async GPU actor retirement, implemented with AIVERSE's existing
// genre-neutral AsyncResourceRetirementQueue. No second fence algorithm.
#pragma once

#include "GpuActorStream.h"
#include "engine/render/AsyncResourceRetirementQueue.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace aiverse::vivid {

// Device-thread-only owner. The gfx::Device MUST outlive the queue and every
// actor it retains. Hosts MUST drain() successfully before destroying either.
// retire()/collect()/drain() are invoked only at a host frame boundary AFTER
// the frame's endFrame() (never while uncommitted GPU work still references
// the model stream). Do not enqueue and delete a Model's source buffers until
// all users of that Model have completed their own retirement.
//
// Several actors queued within one boundary receive ONE ordered GPU token.
// Pending or failed tokens never authorize GPU resource destruction.
// There are no GPU stalls in the normal token-supported collect path.
class GpuActorRetirementQueue final {
public:
    using Resource = std::unique_ptr<GpuActorStream>;
    using Queue = render::AsyncResourceRetirementQueue<Resource>;
    using Stats = Queue::Stats;
    using DiagnosticObserver = Queue::DiagnosticObserver;

    explicit GpuActorRetirementQueue(gfx::Device& device)
        : device_(device) {}
    GpuActorRetirementQueue(const GpuActorRetirementQueue&) = delete;
    GpuActorRetirementQueue& operator=(const GpuActorRetirementQueue&) = delete;
    ~GpuActorRetirementQueue() = default; // caller MUST drain successfully

    // Move one initialized actor into pending retirement, rejecting
    // actors from a different device. On failure caller retains ownership.
    bool retire(Resource& actor);

    // At most one nonblocking GPU marker for all newly pending actors.
    // Returns true iff every actor has been safely released.
    bool collect();

    // Exceptional full-shutdown / device-loss fallback. May block exactly
    // once on the GPU idle barrier. If the barrier fails, all actors remain
    // queued (caller must retry, not destroy the queue or gfx::Device).
    bool drain();

    std::size_t pending() const noexcept { return queue_.pending(); }
    Stats stats() const noexcept { return queue_.stats(); }
    bool saturated(std::size_t maxPending) const noexcept {
        return queue_.saturated(maxPending);
    }
    void setDiagnosticObserver(DiagnosticObserver observer) {
        queue_.setDiagnosticObserver(std::move(observer));
    }
private:
    void release(Resource& actor) noexcept;
    gfx::Device& device_;
    Queue queue_;
};

} // namespace aiverse::vivid
