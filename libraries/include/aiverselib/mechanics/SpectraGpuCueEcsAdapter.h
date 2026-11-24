// SpectraFX Stage 29 — read-only conversion of committed ECS SimEvents
// to GpuCueBridge one-shot requests. No ECS particle handles or genre rules.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/fx/SpectraGpuCueBridge.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <span>
#include <vector>

namespace aiverse::spectra {

// Call once after the gameplay simulation tick is COMMITTED, never from the
// editor/render-frame loop. The bridge deduplicates the committed tick.
// Any source provenance unavailable in SimEvent is deliberately left zero;
// entity-specific cues can instead use GpuCueEvent.source=Entity::packed().
inline bool acceptSimGpuCueTick(GpuCueBridge& bridge,uint64_t tick,
                                 std::span<const SimEvent> events) {
    std::vector<GpuCueEvent> cues;
    cues.reserve(events.size());
    for(const SimEvent& event:events) {
        GpuCueEvent cue;
        cue.kind=event.type;
        cue.position={float(event.pos.x.toDouble()),
                      float(event.pos.y.toDouble()),
                      float(event.pos.z.toDouble())};
        cues.push_back(cue);
    }
    return bridge.accept(tick,cues);
}

} // namespace aiverse::spectra
