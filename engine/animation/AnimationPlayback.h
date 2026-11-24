// Pure animation presentation policies: clip notifies and extract-only root motion.
// Never apply root deltas to authoritative gameplay transforms here.
#pragma once
#include "engine/render/Animation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aiverse::lib::animation {

struct AnimationNotifyKey {
    float time = 0; // seconds in clip
    std::string name;
};
struct AnimationNotifyEvent {
    std::string name;
    double absoluteTime = 0; // unwrapped animation time
    int64_t cycle = 0;
    size_t keyIndex = 0;
};

// (from, to] crossing rule: an event fires once at the time crossed.
// Both clocks are UNWRAPPED seconds. For a newly started clip, use
// from=-epsilon if time-zero notifies should fire. Backwards seek emits none.
// Nullopt signals invalid keys, bad clock, or exceeding maxEvents (no partial
// delivery). Never drive authoritative hit/damage from these view events.
inline std::optional<std::vector<AnimationNotifyEvent>> clipNotifies(
        std::span<const AnimationNotifyKey> keys, float duration,
        double from, double to, bool loop, size_t maxEvents=4096) {
    if (!std::isfinite(duration) || duration<=0.f ||
        !std::isfinite(from) || !std::isfinite(to) ||
        std::abs(from)>1.0e8 || std::abs(to)>1.0e8 ||
        maxEvents>1000000) return std::nullopt;
    std::vector<AnimationNotifyEvent> events;
    if (to<=from) return events; // seek/reverse: no forward notifies
    for (size_t i=0;i<keys.size();++i) {
        const auto& key=keys[i];
        if (!std::isfinite(key.time) || key.time<0 ||
            key.time>duration || (loop && key.time>=duration) ||
            key.name.empty()) return std::nullopt;
        if (!loop) {
            if (double(key.time)>from && double(key.time)<=to) {
                if (events.size()>=maxEvents) return std::nullopt;
                events.push_back({key.name,double(key.time),0,i});
            }
            continue;
        }
        const double first=std::max(0.0,std::floor((from-double(key.time))/duration)+1.0);
        const double last=std::floor((to-double(key.time))/duration);
        if (last<first) continue;
        if (last-first+1.0>double(maxEvents-events.size())) return std::nullopt;
        for (int64_t cycle=int64_t(first); double(cycle)<=last; ++cycle) {
            const double t=double(cycle)*double(duration)+double(key.time);
            if (t>from && t<=to) events.push_back({key.name,t,cycle,i});
        }
    }
    std::stable_sort(events.begin(),events.end(),
        [](const AnimationNotifyEvent& a,const AnimationNotifyEvent& b) {
            return a.absoluteTime<b.absoluteTime ||
                (a.absoluteTime==b.absoluteTime && a.keyIndex<b.keyIndex);
        });
    return events;
}

// A rigid root transform expressed in MODEL space. Clients decide whether
// this is purely visual or a request sent to their authoritative motor.
struct RootMotionTransform {
    render::float3 position{};
    render::quat rotation{};
};
inline RootMotionTransform composeRoot(const RootMotionTransform& a,
                                       const RootMotionTransform& b) {
    return {a.position+a.rotation.rotate(b.position),
            render::qnormalize(a.rotation*b.rotation)};
}
inline RootMotionTransform inverseRoot(const RootMotionTransform& a) {
    const render::quat q=render::qnormalize(
        {-a.rotation.x,-a.rotation.y,-a.rotation.z,a.rotation.w});
    return {q.rotate(-a.position),q};
}
inline RootMotionTransform rootPower(RootMotionTransform base,uint64_t n) {
    RootMotionTransform result;
    while (n>0) {
        if (n&1u) result=composeRoot(result,base);
        n>>=1u;
        if (n) base=composeRoot(base,base);
    }
    return result;
}
inline std::optional<RootMotionTransform> sampleRootAt(
        const render::Model& model, const render::AnimClip& clip,
        int bone, float seconds) {
    if (bone<0 || size_t(bone)>=model.nodes.size() ||
        model.order.size()!=model.nodes.size() || !std::isfinite(seconds))
        return std::nullopt;
    render::Pose pose;
    render::bindPose(model,pose);
    render::sampleClip(clip,seconds,pose);
    std::vector<render::mat4> matrices;
    render::worldMatrices(model,pose,matrices);
    const auto& m=matrices[size_t(bone)];
    const auto rotation=render::rotationOf(m);
    const render::float3 p{m.at(0,3),m.at(1,3),m.at(2,3)};
    for (float v:{p.x,p.y,p.z,rotation.x,rotation.y,rotation.z,rotation.w})
        if (!std::isfinite(v)) return std::nullopt;
    return RootMotionTransform{p,rotation};
}

// Unwraps a looping clip by composing its end/start rigid transform.
// Unlike naive subtraction of samples, this handles crossing arbitrary
// numbers of loop boundaries and accumulated root turns. Returns an
// entity-local delta; it is NEVER applied to Simulation automatically.
inline std::optional<RootMotionTransform> extractClipRootDelta(
        const render::Model& model, const render::AnimClip& clip,
        int rootNode, double from, double to, bool loop) {
    if (!std::isfinite(from) || !std::isfinite(to) ||
        from<0 || to<0 || from>1.e8 || to>1.e8 ||
        !std::isfinite(clip.duration) || clip.duration<=0)
        return std::nullopt;
    const auto begin=sampleRootAt(model,clip,rootNode,0.f);
    const auto end=sampleRootAt(model,clip,rootNode,clip.duration);
    if (!begin || !end) return std::nullopt;
    const auto cycle=composeRoot(*end,inverseRoot(*begin));
    auto absolute=[&](double t)->std::optional<RootMotionTransform> {
        uint64_t k=0;
        float local=0.f;
        if (loop) {
            k=uint64_t(std::floor(t/clip.duration));
            local=float(t-double(k)*double(clip.duration));
            local=std::clamp(local,0.f,clip.duration);
        } else local=float(std::min(t,double(clip.duration)));
        auto sampled=sampleRootAt(model,clip,rootNode,local);
        if (!sampled) return std::nullopt;
        return loop?composeRoot(rootPower(cycle,k),*sampled):*sampled;
    };
    const auto a=absolute(from),b=absolute(to);
    if (!a || !b) return std::nullopt;
    return composeRoot(inverseRoot(*a),*b);
}
} // namespace aiverse::lib::animation
