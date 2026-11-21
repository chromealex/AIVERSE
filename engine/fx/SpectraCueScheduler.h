// AIVERSE SpectraFX: genre-neutral, presentation-only VFX cue scheduler.
// No gameplay ownership, event enums, render handles in ECS, or background work.
// Uses one existing BatchWorld for all CPU graph/effect instances.
// The host controls when BatchWorld::update()/draw() run, exactly once per frame.
#pragma once

#include "SpectraBatchWorld.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct CueBudgets {
    size_t maxQueued = 512;           // pending across frames
    size_t maxPerTick = 64;           // accepted one-shot events per committed tick
    size_t maxPerFrame = 128;         // one-shot births per presentation frame
    size_t maxAttachments = 1024;    // active ECS/world sources
    size_t maxTrackedOneShots = 2048;// at least one; older tracked cues killed on overflow
};

struct CueEvent {
    uint32_t type = 0;               // caller's semantic event ID, no genre enum
    fx::float3 position{};
    fx::float3 direction{0,1,0};
    float scale = 1.0f;
    fx::float4 tint{1,1,1,1};
    // Optional per-cue VM scalar uniforms param0..param7 (bit i set).
    std::array<float,8> programParams{};
    uint8_t programParamMask=0;
};

struct AttachedCue {
    uint64_t key = 0;                // stable identity INCLUDING ECS generation
    std::string effect;              // registered effect/graph key
    fx::float3 position{};
    fx::float3 direction{0,1,0};
    float scale = 1.0f;
    uint32_t revision = 0;           // content/pose policy change restarts emitter
    bool killOnDetach = false;       // false = stop emission, let particles fade
    // Presentation-only overrides; changing these does not restart emitter.
    std::array<float,8> programParams{};
    uint8_t programParamMask=0;
};

struct CueSchedulerStats {
    uint64_t lastTick = 0;
    uint64_t acceptedTicks = 0, duplicateTicks = 0;
    uint64_t acceptedCues = 0, emittedCues = 0;
    uint64_t droppedBudget = 0, droppedUnbound = 0, failedSpawns = 0;
    uint64_t evictedOneShots = 0;
    uint64_t stoppedSources = 0, restartedSources = 0;
    size_t queued = 0, attached = 0, trackedOneShots = 0;
};

// The caller owns BatchWorld and must destroy this scheduler FIRST.
// All source/event handles live exclusively in this presentation scheduler.
// BatchWorld keeps CPU simulation/batching; Metal GPU graphs can still be
// managed separately by their host but are not silently started here.
// This class is intentionally not a generic Niagara GPU runtime.
class CueScheduler final {
public:
    explicit CueScheduler(BatchWorld& world, CueBudgets budgets = {});
    ~CueScheduler() { resetForRollback(); }
    CueScheduler(const CueScheduler&) = delete;
    CueScheduler& operator=(const CueScheduler&) = delete;

    // Fully compiled definitions avoid per-event parsing and are shareable
    // among different names/genres. Replacing a name affects future cues;
    // attached sources automatically restart on their next sync.
    bool registerEffect(const std::string& name, const fx::EffectDef& definition);
    bool registerGraph(const std::string& name, const Json& graph,
                       const Json& overrides = Json::object());
    bool bindCue(uint32_t type, const std::string& effectName);
    void unbindCue(uint32_t type);

    // Each committed gameplay tick is accepted at most once, even across
    // multiple render frames. Replay/rollback REQUIRES resetForRollback().
    // One-shot cues are queued, never submitted mid-render-pass by this call.
    bool acceptTick(uint64_t tick, std::span<const CueEvent> events);
    // Presentation-frame boundary: spawn at most maxPerFrame queued cues.
    // Call before BatchWorld::update(), then draw the world exactly once.
    size_t flush();

    // Caller passes the entire visible/attached source set each presentation
    // frame; omitted sources are stopped/killed. Only continuous effects
    // should be attached. Existing key+revision+effect+scale preserves handle.
    // Stable source key should include ECS entity generation.
    void syncAttachments(std::span<const AttachedCue> sources);

    // Kill ALL presentation-owned source/cue particles (even cues which
    // stopped emitting but still have live particles). Clear queue and tick
    // cursor. Leave registered effect definitions and bindings intact.
    void resetForRollback();

    const CueSchedulerStats& stats() const { return stats_; }
    const CueBudgets& budgets() const { return budgets_; }
    bool haveTick() const { return haveTick_; }
    size_t registeredEffects() const { return effects_.size(); }
    const std::string& lastError() const { return error_; }
private:
    struct EffectRef { int id = -1; };
    struct Active {
        fx::FxHandle handle;
        int effectId = -1;
        std::string effectName;
        uint32_t revision = 0;
        float scale = 1.0f;
        bool killOnDetach = false;
        std::array<float,8> programParams{};
        uint8_t programParamMask=0;
    };
    struct Queued {
        int effectId = -1;
        CueEvent cue;
    };
    void retire(const Active& active);
    void trackOneShot(fx::FxHandle handle);
    BatchWorld& world_;
    CueBudgets budgets_;
    CueSchedulerStats stats_;
    std::map<std::string, EffectRef> effects_;
    std::map<uint32_t, std::string> bindings_;
    std::map<uint64_t, Active> active_;
    std::deque<Queued> queue_;
    std::deque<fx::FxHandle> oneShots_;
    bool haveTick_ = false;
    std::string error_;
};

} // namespace aiverse::spectra
