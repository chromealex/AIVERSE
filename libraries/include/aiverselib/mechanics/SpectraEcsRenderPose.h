// SpectraFX Stage 38 — read-only render-pose interpolation for ECS sources.
// Two committed-pose snapshots live ONLY in the presentation layer, never
// inside the authoritative gameplay simulation, ECS hash or network state.
#pragma once
#include "engine/mechanics/Namespace.h"

#include "SpectraGpuAttachmentEcsAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace aiverse::spectra {

struct SpectraRenderPose {
    fx::float3 position{},direction{0,1,0};
};

// The native game owns the source World and its pose resolver. The presenter
// samples committed positions once per new tick and may smoothly interpolate
// on multiple rendered frames of that same tick. A recycled Entity index has
// a different packed generation, so there is no cross-life interpolation.
class GpuEcsPoseInterpolator final {
public:
    using ResolvePose=GpuEcsAttachmentPresenter::ResolvePose;

    explicit GpuEcsPoseInterpolator(uint32_t maxSources=4096)
        : maxSources_(std::clamp<uint32_t>(maxSources,1,65536)) {}

    bool captureCommitted(uint64_t tick,const World& world,
                          const ResolvePose& resolver) {
        if(!resolver || (haveTick_ && tick<lastTick_))return false;
        if(haveTick_ && tick==lastTick_)return true; // immutable committed tick

        std::map<uint64_t,SpectraRenderPose> staged;
        bool valid=true;
        world.eachExistingConst<lib::mechanics::SpectraSourceComponent>(
            [&](Entity entity,const lib::mechanics::SpectraSourceComponent& source) {
                if(!valid || !world.alive(entity) || !source.enabled ||
                   source.effect.empty())return;
                fx::float3 p{},d{0,1,0};
                if(!resolver(world,entity,p,d))return; // no pose: pause source
                if(!finite(p) || !finite(d) || staged.size()>=maxSources_) {
                    valid=false;return;
                }
                staged.emplace(entity.packed(),SpectraRenderPose{p,d});
            });
        if(!valid)return false; // retain old history; fail atomically
        const bool adjacent=haveTick_ && tick==lastTick_+1;
        previous_=adjacent?std::move(current_)
                          :std::map<uint64_t,SpectraRenderPose>{};
        current_=std::move(staged);
        lastTick_=tick;
        haveTick_=true;
        return true;
    }

    bool sample(Entity entity,float alpha,fx::float3& p,fx::float3& d) const {
        if(!haveTick_ || !entity.valid() || !std::isfinite(alpha))return false;
        auto current=current_.find(entity.packed());
        if(current==current_.end())return false;
        p=current->second.position;
        d=current->second.direction;
        auto previous=previous_.find(entity.packed());
        if(previous==previous_.end())return true;
        const float t=std::clamp(alpha,0.f,1.f);
        const auto& q=previous->second;
        p={
            q.position.x+(p.x-q.position.x)*t,
            q.position.y+(p.y-q.position.y)*t,
            q.position.z+(p.z-q.position.z)*t
        };
        d={
            q.direction.x+(d.x-q.direction.x)*t,
            q.direction.y+(d.y-q.direction.y)*t,
            q.direction.z+(d.z-q.direction.z)*t
        };
        const float lengthSq=d.x*d.x+d.y*d.y+d.z*d.z;
        if(std::isfinite(lengthSq) && lengthSq>1.e-10f) {
            const float inv=1.f/std::sqrt(lengthSq);
            d.x*=inv;d.y*=inv;d.z*=inv;
        } else d={0,1,0};
        return finite(p)&&finite(d);
    }

    // The returned resolver captures this by pointer. It must never outlive
    // the GpuEcsPoseInterpolator and must be used on the presentation thread.
    ResolvePose resolver(float alpha) const {
        return [this,alpha](const World&,Entity entity,
                            fx::float3& p,fx::float3& d) {
            return sample(entity,alpha,p,d);
        };
    }

    void resetForRollback() {
        current_.clear();previous_.clear();
        lastTick_=0;haveTick_=false;
    }
    uint64_t committedTick() const {return lastTick_;}
    size_t currentSourceCount() const {return current_.size();}
    bool hasSnapshot() const {return haveTick_;}

private:
    static bool finite(fx::float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    uint32_t maxSources_=4096;
    std::map<uint64_t,SpectraRenderPose> current_,previous_;
    uint64_t lastTick_=0;
    bool haveTick_=false;
};

} // namespace aiverse::spectra
