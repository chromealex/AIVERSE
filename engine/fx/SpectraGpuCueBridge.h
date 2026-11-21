// AIVERSE SpectraFX Stage 29: presentation-only committed-tick cue adapter
// for GpuPoolWorld. Do not confuse one-shot GPU bursts with persistent sources.
// No gameplay ECS mutation, genre dependency, GPU readbacks or second renderer.
#pragma once
#include "SpectraGpuPoolWorld.h"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct GpuCueBudgets {
    uint32_t queued = 512;
    uint32_t acceptedPerTick = 64;
    uint32_t dispatchedPerFrame = 128;
};
struct GpuCueEvent {
    uint32_t kind=0; // game-defined semantic code
    uint64_t source=0; // optional generation-safe entity ID; 0 for unbound events
    fx::float3 position{};
    fx::float3 direction{0,1,0};
    fx::float4 tint{1,1,1,1};
    // Overrides use a full snapshot, not mutable group-wide parameter state.
    std::array<float,8> parameters{};
    bool hasParameters=false;
};
struct GpuCueStats {
    uint64_t lastTick=0;
    uint64_t committedTicks=0;
    uint64_t duplicateTicks=0;
    uint64_t accepted=0;
    uint64_t submitted=0;
    uint64_t droppedBudget=0;
    uint64_t droppedUnbound=0;
    uint64_t rejectedByWorld=0; // dropped after kMaxFlushAttempts rejections
    uint64_t deferredByWorld=0; // rejected once, kept for the next flush
    uint32_t queued=0;
};

// A small one-shot scheduler on top of an existing, caller-owned GpuPoolWorld.
// The world MUST outlive the bridge. It is intentionally separate from
// CueScheduler/BatchWorld: this bridge does not create another particle world.
class GpuCueBridge final {
public:
    explicit GpuCueBridge(GpuPoolWorld& world, GpuCueBudgets budgets = {});
    GpuCueBridge(const GpuCueBridge&)=delete;
    GpuCueBridge& operator=(const GpuCueBridge&)=delete;

    // Register a generic event type, with an already-defined pooled effect.
    // Rebinding is immediate for *future* accepted ticks; queued events
    // snapshot their effect key and are not retroactively changed.
    bool bind(uint32_t kind, std::string effectName);
    void unbind(uint32_t kind) {bindings_.erase(kind);}

    // Called once for each *committed* gameplay tick, not each render frame.
    // On rollback the host must call resetForRollback() before replaying ticks.
    bool accept(uint64_t tick, std::span<const GpuCueEvent> events);
    // Called between GpuPoolWorld::beginPresentationFrame and ::step.
    // Submission failure is not silently counted as emitted. A rejected
    // event (e.g. the world's per-frame burst budget is full) stays queued,
    // in order, for the next flush; after kMaxFlushAttempts rejections it is
    // dropped so a permanently invalid cue cannot block the queue.
    static constexpr uint8_t kMaxFlushAttempts = 4;
    uint32_t flush();

    // Resets ALL visual effects registered in the GpuPoolWorld and clears
    // queued cues, tick cursor and counters. This may not be used when other
    // independent owners need to preserve particles from the same pool.
    // Must run outside active graphics passes. Failure leaves bridge state
    // unchanged so replay can be retried safely.
    bool resetForRollback();

    const GpuCueStats& stats() const {return stats_;}
    const std::string& reason() const {return reason_;}
    GpuCueBudgets budgets() const {return budgets_;}
private:
    struct Queued { std::string effect; GpuCueEvent cue; uint8_t attempts=0; };
    GpuPoolWorld& world_;
    GpuCueBudgets budgets_;
    GpuCueStats stats_{};
    std::map<uint32_t,std::string> bindings_;
    std::deque<Queued> queued_;
    bool haveTick_=false;
    std::string reason_;
};

} // namespace aiverse::spectra
