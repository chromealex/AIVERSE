// Presentation animation event bus: Graph Clip notifies + Montage cues.
// Routes cosmetic events to a host callback only, never simulation commands.
#pragma once
#include "EcsAnimationSystems.h"
#include <algorithm>
#include <string>
#include <vector>
namespace aiverse::lib::animation {
struct AnimationCue {
    Entity entity=kNullEntity;
    std::string name;
    std::string source; // "graph:<node>" or "montage"
    float framePhase=0.f;
    uint64_t order=0;
};
// Returns a stable sorted list for the last animation update.
// Deliberately does not mutate any ECS component; repeated collection of
// the same frame returns the same cues. Host must gate one delivery/frame.
inline std::vector<AnimationCue> collectAnimationCues(const World& world,
                                                     size_t maxEvents=8192) {
    std::vector<AnimationCue> cues;
    auto* players=world.poolIfExists<AnimationAssetComponent>();
    if(!players)return cues;
    uint64_t sequence=0;
    for(size_t i=0;i<players->size() && cues.size()<maxEvents;++i) {
        const Entity entity=players->entityAt(i);
        if(!world.alive(entity))continue;
        if(auto* g=world.tryGetConst<AnimationGraphNotifiesComponent>(entity))
            for(const auto& event:g->events) {
                if(cues.size()>=maxEvents)break;
                cues.push_back({entity,event.name,"graph:"+event.node,
                    event.framePhase,sequence++});
            }
        if(auto* m=world.tryGetConst<AnimationMontageSlotsComponent>(entity))
            for(const auto& event:m->events) {
                if(cues.size()>=maxEvents)break;
                cues.push_back({entity,event.name,"montage",0.5f,sequence++});
            }
    }
    std::stable_sort(cues.begin(),cues.end(),
        [](const AnimationCue& a,const AnimationCue& b) {
            return a.framePhase!=b.framePhase?a.framePhase<b.framePhase:
                a.order<b.order;
        });
    return cues;
}
template<class CosmeticSink>
void dispatchAnimationCues(const World& world,CosmeticSink&& sink,
                           size_t maxEvents=8192) {
    for(auto& cue:collectAnimationCues(world,maxEvents))sink(cue);
}
} // namespace aiverse::lib::animation
