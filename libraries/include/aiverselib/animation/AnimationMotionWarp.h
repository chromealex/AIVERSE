// Presentation-space motion warping over a single non-looping clip.
// Calculates requested root deltas; NEVER moves authoritative ECS entities.
#pragma once
#include "AnimationPlayback.h"
#include <algorithm>
#include <cmath>
#include <optional>
namespace aiverse::lib::animation {
struct WarpWindow {
    float begin=0, end=1; // normalized clip progress, 0 <= begin < end <= 1
};
inline float warpWeight(float progress,WarpWindow window) {
    const float u=std::clamp((progress-window.begin)/(window.end-window.begin),0.f,1.f);
    return u*u*(3.f-2.f*u);
}
// Warped absolute root transform since start of clip. Correction eases into
// target during a configured window. Targets must be supplied in clip/model
// local coordinates by the caller, not in world space. Root turns are composed.
inline std::optional<RootMotionTransform> warpedRootAt(
        const render::Model& model,const render::AnimClip& clip,int root,
        double t,const RootMotionTransform& target,WarpWindow window={}) {
    if (!std::isfinite(t) || t<0 || !std::isfinite(clip.duration) ||
        clip.duration<=0 || !(window.begin>=0) ||
        !(window.end>window.begin) || !(window.end<=1) ||
        !std::isfinite(target.position.x) || !std::isfinite(target.position.y) ||
        !std::isfinite(target.position.z) ||
        !std::isfinite(target.rotation.x) || !std::isfinite(target.rotation.y) ||
        !std::isfinite(target.rotation.z) || !std::isfinite(target.rotation.w))
        return std::nullopt;
    const auto current=extractClipRootDelta(model,clip,root,0,std::min(t,double(clip.duration)),false);
    const auto last=extractClipRootDelta(model,clip,root,0,clip.duration,false);
    if (!current || !last) return std::nullopt;
    const float u=warpWeight(float(std::min(t,double(clip.duration))/clip.duration),window);
    const render::quat inv{
        -last->rotation.x,-last->rotation.y,-last->rotation.z,last->rotation.w};
    const render::quat correction=render::qnormalize(inv*target.rotation);
    RootMotionTransform out;
    out.position=current->position+(target.position-last->position)*u;
    out.rotation=render::qnormalize(
        current->rotation*render::slerp(render::quat{},correction,u));
    return out;
}
// Relative delta between two warped clip times for host-owned motor request.
inline std::optional<RootMotionTransform> warpedRootDelta(
        const render::Model& model,const render::AnimClip& clip,int root,
        double from,double to,const RootMotionTransform& target,
        WarpWindow window={}) {
    if (from>to) return std::nullopt;
    const auto a=warpedRootAt(model,clip,root,from,target,window);
    const auto b=warpedRootAt(model,clip,root,to,target,window);
    if (!a || !b) return std::nullopt;
    return composeRoot(inverseRoot(*a),*b);
}
} // namespace aiverse::lib::animation
