// Blends externally simulated bone transforms into an AnimationGraph pose.
// This is a presentation adapter only, not a ragdoll solver or physics world.
#pragma once
#include "engine/render/Animation.h"
#include <algorithm>
#include <cmath>
#include <span>
namespace aiverse::lib::animation {
inline bool validPhysicalNode(const render::NodePose& n) {
    for (float value:{n.t.x,n.t.y,n.t.z,n.r.x,n.r.y,n.r.z,n.r.w,
                      n.s.x,n.s.y,n.s.z})
        if (!std::isfinite(value)) return false;
    for (float value:n.w) if (!std::isfinite(value)) return false;
    return render::dot(n.r,n.r)>1.e-8f;
}
// Local poses MUST share the same skeleton/bone indexing. The host may
// convert external world-space rigidbody transforms into bone-local TRS.
// Empty boneWeights means all bones. No writes occur on invalid input.
inline bool blendPhysicalPose(render::Pose& animated,
                              const render::Pose& physics,
                              std::span<const float> boneWeights={},
                              float weight=1.f) {
    if (!std::isfinite(weight) || weight<0.f || weight>1.f ||
        animated.size()!=physics.size() ||
        (!boneWeights.empty() && boneWeights.size()!=animated.size()))
        return false;
    for (size_t i=0;i<physics.size();++i) {
        if (!validPhysicalNode(physics[i]) || !validPhysicalNode(animated[i]))
            return false;
        if (!boneWeights.empty() && (!std::isfinite(boneWeights[i]) ||
                                      boneWeights[i]<0.f ||
                                      boneWeights[i]>1.f)) return false;
    }
    if (weight==0.f) return true;
    for (size_t i=0;i<physics.size();++i) {
        const float w=weight*(boneWeights.empty()?1.f:boneWeights[i]);
        if (w<=0.f) continue;
        auto& a=animated[i];
        const auto& p=physics[i];
        a.t=render::lerp(a.t,p.t,w);
        a.r=render::slerp(render::qnormalize(a.r),render::qnormalize(p.r),w);
        a.s=render::lerp(a.s,p.s,w);
        for (int k=0;k<render::kMaxMorphTargets;++k)
            a.w[k]+=(p.w[k]-a.w[k])*w;
    }
    return true;
}
} // namespace aiverse::lib::animation
