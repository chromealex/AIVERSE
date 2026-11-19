// Per-bone inertialization: decaying positional/angular offsets with initial
// relative velocity and zero residual/velocity at the end of the transition.
// Presentation-only: host provides source/target pose and optional velocities.
#pragma once
#include "engine/render/Animation.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace aiverse::lib::animation {
struct BonePoseVelocity {
    render::float3 translation{},angular{},scale{};
    std::array<float,render::kMaxMorphTargets> morph{};
};
inline render::float3 quaternionLog(render::quat q) {
    q=render::qnormalize(q);
    if (q.w<0) q={-q.x,-q.y,-q.z,-q.w};
    render::float3 axis{q.x,q.y,q.z};
    const float len=render::length(axis);
    if (len<1.e-7f) return axis*2.f;
    return axis*(2.f*std::atan2(len,q.w)/len);
}
inline render::quat quaternionExp(render::float3 r) {
    const float angle=render::length(r);
    if (angle<1.e-6f) return render::qnormalize({r.x*.5f,r.y*.5f,r.z*.5f,1.f});
    const float scale=std::sin(angle*.5f)/angle;
    return render::qnormalize({r.x*scale,r.y*scale,r.z*scale,std::cos(angle*.5f)});
}
inline float inertialResidual(float offset,float velocity,float t,float T) {
    const float x=std::clamp(t/T,0.f,1.f);
    // Hermite cubic: x(0)=offset, x'(0)=velocity, x(T)=x'(T)=0.
    return offset*(1.f-3.f*x*x+2.f*x*x*x)
         + velocity*T*(x-2.f*x*x+x*x*x);
}
inline render::float3 inertialResidual(render::float3 offset,
                                    render::float3 velocity,float t,float T) {
    return {inertialResidual(offset.x,velocity.x,t,T),
            inertialResidual(offset.y,velocity.y,t,T),
            inertialResidual(offset.z,velocity.z,t,T)};
}
class PoseInertializer {
public:
    // Velocities are in local bone space, units/radians per second.
    // Derived sampling velocities must come from the host's prior poses.
    void begin(const render::Pose& outgoing,const render::Pose& incoming,
               float duration,
               const std::vector<BonePoseVelocity>& fromVelocity={},
               const std::vector<BonePoseVelocity>& toVelocity={}) {
        if (outgoing.size()!=incoming.size() || outgoing.empty() ||
            !std::isfinite(duration) || duration<=0 ||
            (!fromVelocity.empty() && fromVelocity.size()!=outgoing.size()) ||
            (!toVelocity.empty() && toVelocity.size()!=outgoing.size()))
            throw std::invalid_argument("inertialization: invalid pose/velocity");
        nodes_.clear();
        nodes_.reserve(outgoing.size());
        for (size_t i=0;i<outgoing.size();++i) {
            const BonePoseVelocity prev=fromVelocity.empty()?BonePoseVelocity{}:fromVelocity[i];
            const BonePoseVelocity next=toVelocity.empty()?BonePoseVelocity{}:toVelocity[i];
            Node n;
            n.translation=outgoing[i].t-incoming[i].t;
            n.vTranslation=prev.translation-next.translation;
            n.scale=outgoing[i].s-incoming[i].s;
            n.vScale=prev.scale-next.scale;
            const auto& a=outgoing[i].r;
            const auto& b=incoming[i].r;
            render::quat inverse{-b.x,-b.y,-b.z,b.w};
            n.angular=quaternionLog(a*inverse);
            n.vAngular=prev.angular-next.angular;
            for(int k=0;k<render::kMaxMorphTargets;++k) {
                n.morph[size_t(k)]=outgoing[i].w[k]-incoming[i].w[k];
                n.vMorph[size_t(k)]=prev.morph[size_t(k)]-next.morph[size_t(k)];
            }
            nodes_.push_back(n);
        }
        duration_=duration;time_=0;
    }
    bool active() const {return !nodes_.empty() && time_<duration_;}
    void advance(float dt) {
        if (!std::isfinite(dt) || dt<0) return;
        time_=std::min(duration_,time_+dt);
    }
    // Applies decaying offset to an already-evaluated incoming graph pose.
    void apply(render::Pose& incoming) const {
        if (nodes_.empty())return;
        if (incoming.size()!=nodes_.size())throw std::invalid_argument("inertialization: pose layout changed");
        for (size_t i=0;i<nodes_.size();++i) {
            const auto& n=nodes_[i];
            auto& p=incoming[i];
            p.t+=inertialResidual(n.translation,n.vTranslation,time_,duration_);
            p.s+=inertialResidual(n.scale,n.vScale,time_,duration_);
            p.r=render::qnormalize(quaternionExp(
                inertialResidual(n.angular,n.vAngular,time_,duration_))*p.r);
            for(int k=0;k<render::kMaxMorphTargets;++k)
                p.w[k]+=inertialResidual(n.morph[size_t(k)],n.vMorph[size_t(k)],time_,duration_);
        }
    }
    void reset(){nodes_.clear();time_=duration_=0;}
private:
    struct Node {
        render::float3 translation{},vTranslation{},angular{},vAngular{},scale{},vScale{};
        std::array<float,render::kMaxMorphTargets> morph{},vMorph{};
    };
    std::vector<Node> nodes_;
    float duration_=0,time_=0;
};
} // namespace aiverse::lib::animation
