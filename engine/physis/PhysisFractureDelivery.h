// PHYSIS Stage 84 — bounded authoritative host delivery of nature fractures.
// This is a genre-neutral control-flow helper, not a physics or terrain adapter.
// The host sink MUST durably stage/deduplicate external effects before reporting
// DurablyCommitted; a transient Box3D spawn by itself does not qualify.
#pragma once
#include "PhysisFractureOutbox.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::physis {

enum class NatureFractureDisposition : std::uint8_t {
    Retry = 0,
    DurablyCommitted = 1
};

struct NatureFractureDispatchStats {
    std::size_t pendingAtStart = 0;
    std::size_t visited = 0;
    std::size_t committed = 0;
    std::size_t retry = 0;
    std::size_t stale = 0;
    std::size_t pendingAtEnd = 0;
};

// Run only on the authoritative GAME/HOST barrier, outside PHYSIS system
// iteration. The sink is called once per visited event, in stable Entity order,
// with a by-value event snapshot. It must:
//   - persist/deduplicate by (entity.packed(), climateTick, eventKey),
//   - stage the physical/terrain consequence idempotently,
//   - return DurablyCommitted ONLY when the host's own durable state is safe.
// A Retry does not acknowledge the event; it is delivered on the next call.
// A thrown sink exception propagates: previously acknowledged events stay
// acknowledged; the failing event and remaining events are left pending.
// Dispatch is NOT a distributed transaction with Box3D/TerraWeave journals.
// After rollback, the host must reconcile its durable effects independently.
// maxEvents bounds sink calls (not snapshot scan/sort memory).
template<class DurableSink>
inline NatureFractureDispatchStats dispatchNatureFractures(
        World& world, DurableSink&& sink, std::size_t maxEvents = 64) {
    if (maxEvents == 0 || maxEvents > 4096)
        throw std::invalid_argument("PHYSIS fracture delivery budget must be 1..4096");

    const auto pending = pendingNatureFractures(world);
    NatureFractureDispatchStats stats;
    stats.pendingAtStart = pending.size();
    const std::size_t count = std::min(maxEvents, pending.size());
    for (std::size_t i = 0; i < count; ++i) {
        const NatureFractureEvent& event = pending[i];
        // A previous host operation in this same dispatch may have changed
        // World; recheck generation and identity before calling the sink.
        if (!world.alive(event.entity)) {
            ++stats.stale;
            continue;
        }
        const auto* current = static_cast<const World&>(world)
            .tryGetConst<NatureFractureOutboxComponent>(event.entity);
        if (!current ||
            current->delivery != NatureFractureDelivery::Pending ||
            current->eventKey != event.eventKey ||
            current->climateTick != event.climateTick) {
            ++stats.stale;
            continue;
        }
        ++stats.visited;
        const NatureFractureDisposition disposition = sink(event);
        if (disposition == NatureFractureDisposition::Retry) {
            ++stats.retry;
        } else if (disposition == NatureFractureDisposition::DurablyCommitted) {
            if (acknowledgeNatureFracture(
                    world, event.entity, event.eventKey, event.climateTick))
                ++stats.committed;
            else
                ++stats.stale; // sink changed ECS; host must reconcile
        } else {
            throw std::invalid_argument("invalid PHYSIS fracture sink disposition");
        }
    }
    stats.pendingAtEnd = pendingNatureFractures(world).size();
    return stats;
}

} // namespace aiverse::physis
