// FPS-only translation of authoritative actor counters to animation cues.
// This header belongs to the host, never to the MotionWeave library.
#pragma once
#include "engine/animation/AnimationActionEdges.h"
#include <cstdint>
namespace aiverse::actor::view {
struct ReplicatedAnimationState {
    uint32_t tick=0,life=0,shots=0,reloadUntil=0;
    int32_t health=0;
};
struct CosmeticAnimationActions {
    bool fire=false,reload=false,death=false,respawn=false;
};
class ReplicatedAnimationActions {
public:
    CosmeticAnimationActions observe(ReplicatedAnimationState src) {
        lib::animation::AnimationActionFrame frame;
        frame.revision=src.tick;frame.generation=src.life;
        frame.counters["shot"]=src.shots;
        frame.counters["reload"]=src.health>0 && src.reloadUntil>src.tick
            ?src.reloadUntil:0;
        frame.flags["dead"]=src.health<=0;
        const auto edges=events_.observe(frame);
        return {edges.increasedCounter("shot") && src.health>0,
                edges.increasedCounter("reload") && src.health>0,
                edges.becameActive("dead"),edges.generationChanged};
    }
    void reset(){events_.reset();}
private:
    lib::animation::AnimationActionEdges events_;
};
} // namespace aiverse::actor::view
