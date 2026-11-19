// MotionWeave normalized phase capture for cosmetic CPU/GPU backend switches.
// Only clip-local presentation state; never touches authoritative actor ticks.
#pragma once
#include "engine/render/AnimationGraph.h"
#include "engine/render/Animation.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace aiverse::lib::animation {
inline std::optional<float> normalizedClipPhase(float seconds,float duration) {
    if(!std::isfinite(seconds)||!std::isfinite(duration)||duration<=0)
        return std::nullopt;
    const float phase=std::fmod(std::max(0.f,seconds)/duration,1.f);
    return std::clamp(phase,0.f,.999999f);
}
inline std::optional<float> captureBakedClipPhase(
        const render::Animator& anim,const render::Model& model) {
    const int clip=anim.clip(0);
    if(clip<0 || size_t(clip)>=model.clips.size())return std::nullopt;
    return normalizedClipPhase(anim.time(0),model.clips[size_t(clip)].duration);
}
inline std::optional<float> captureGraphClipPhase(
        const render::AnimationGraphInstance& graph,
        const render::Model& model,bool moving) {
    const auto* program=graph.graph();
    if(!program || program->model()!=&model)return std::nullopt;
    for(const char* id : moving?std::initializer_list<const char*>{"move","idle"}:
                                 std::initializer_list<const char*>{"idle","move"}) {
        const auto time=graph.clipTime(id);
        if(!time)continue;
        for(const auto& n:program->nodes())
            if(n.id==id && n.kind==render::AnimationGraph::Kind::Clip &&
               n.clip>=0 && size_t(n.clip)<model.clips.size())
                return normalizedClipPhase(*time,model.clips[size_t(n.clip)].duration);
    }
    return std::nullopt;
}
} // namespace aiverse::lib::animation
