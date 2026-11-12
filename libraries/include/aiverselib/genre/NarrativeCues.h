#pragma once
// Cinematic cue requests emitted by narrative scenes (narrative-graphs 0.2.0). Pure data: the narrative policy
// never plays, times, skips or blocks on a cue. A cinematic/presentation owner consumes them in `sequence` order.
// `sequence` is strictly increasing per NarrativeState (it is saved), so after restore/rollback a consumer that
// remembers the last handled sequence never replays or loses a request.
#include <cstdint>
namespace aiverse::lib::genre {
enum class CueSource:uint8_t {NodeEnter,Choice,Consequence,Bark};
struct CueRequest {
    uint64_t sequence=0;              // 1, 2, 3, ... per NarrativeState
    int64_t cueId=0;                  // data-defined stable cue ID (> 0); its meaning belongs to the cue owner
    int64_t nodeId=0;                 // line that requested it (NodeEnter/Choice; 0 for consequences and barks)
    int64_t choiceId=0;               // choice taken (Choice only)
    int64_t sceneId=0;                // active scene (0 outside scenes)
    int64_t speaker=0;                // speaking character of the node/bark (0 = narrator or none)
    int64_t barkId=0,consequenceId=0; // Bark / Consequence sources
    uint32_t tick=0;                  // authoritative tick of the request
    uint32_t delayTicks=0;            // data offset after `tick`; the cue owner schedules it
    CueSource source=CueSource::NodeEnter;
};
}
