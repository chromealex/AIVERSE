// NEREID Stage 37 — deterministic local flow continuity at linked cascades.
// A read-only sampling policy. It does not transfer mass, integrate motion,
// modify the canonical water body, or depend on a genre/PHYSIS/Box3D.
#pragma once
#include "NereidCascades.h"
#include "NereidVortices.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace aiverse::lib::nereid {

inline bool validWaterCascadeFlowPolicy(
        const WaterCascadeFlowPolicyComponent& policy) {
    return policy.enabled && policy.radius>=Fixed::fromRatio(1,10) &&
           policy.radius<=1000_fx && policy.blend>=0_fx &&
           policy.blend<=1_fx && policy.speedLimit>0_fx &&
           policy.speedLimit<=1000_fx;
}

inline Vec2 cascadeDownstreamDirection(
        const WaterCascadeComponent& cascade,bool atFoot) {
    if(cascade.points.size()<2)return {};
    const size_t j=atFoot?cascade.points.size()-1:1;
    const size_t i=j-1;
    const auto& p=cascade.points[i].position;
    const auto& q=cascade.points[j].position;
    Vec2 d{q.x-p.x,q.z-p.z};
    if(d.lengthSq()<=Fixed{})
        d={-cascade.widthAxis.y,cascade.widthAxis.x};
    const Fixed n=d.length();
    return n>Fixed{}?d/n:Vec2{};
}

struct WaterCascadeFlowContribution {
    uint64_t cascadeId=0;
    Vec2 desired{};
    Fixed weight{};
};

// Shared deterministic mouth kernel used by authoritative ECS queries and
// the optional read-only scene presentation current. No dynamic allocations.
inline WaterCascadeFlowContribution waterCascadeMouthContribution(
        uint64_t id,const WaterCascadeComponent& cascade,
        const WaterCascadeFlowPolicyComponent& policy,bool atOutlet,
        Vec3 position,Fixed upstreamSpeed=Fixed{}) {
    WaterCascadeFlowContribution out;
    if(!validWaterCascadeFlowPolicy(policy) || !validWaterCascade(cascade))
        return out;
    const Vec3 anchor=atOutlet?cascade.points.back().position:
                              cascade.points.front().position;
    const Fixed distance=(position.xz()-anchor.xz()).length();
    if(distance>=policy.radius)return out;
    const Vec2 heading=cascadeDownstreamDirection(cascade,atOutlet);
    if(heading.lengthSq()<=Fixed{})return out;
    Fixed speed=cascade.speed;
    if(atOutlet)speed=fxMax(speed,upstreamSpeed);
    speed=fxClamp(speed,Fixed{},policy.speedLimit);
    const Fixed t=fxClamp(1_fx-distance/policy.radius,0_fx,1_fx);
    const Fixed weight=policy.blend*t*t*(3_fx-t*2);
    if(weight<=Fixed{})return out;
    return {id,heading*speed,weight};
}

template<size_t N>
inline Vec3 blendWaterCascadeContributions(
        Vec3 base,std::array<WaterCascadeFlowContribution,N>& entries,
        size_t used) {
    if(!used)return base;
    used=std::min(used,N);
    std::sort(entries.begin(),entries.begin()+used,
        [](const auto& a,const auto& b) {
            return a.cascadeId<b.cascadeId;
        });
    Vec2 weighted{};
    Fixed total{};
    for(size_t i=0;i<used;++i) {
        weighted+=entries[i].desired*entries[i].weight;
        total+=entries[i].weight;
    }
    if(total<=Fixed{})return base;
    const Vec2 desired=weighted/total;
    const Fixed alpha=fxClamp(total,0_fx,1_fx);
    const Vec2 initial=base.xz();
    const Vec2 result=initial*(1_fx-alpha)+desired*alpha;
    base.x=fxClamp(result.x,-1000_fx,1000_fx);
    base.z=fxClamp(result.y,-1000_fx,1000_fx);
    return base;
}

// After regular water body / current / vortex sampling, near a waterfall
// mouth steer the local WaterSample.flow toward its linked channel. Multiple
// waterfalls are combined in stable Entity-ID order, independent of pool
// insertion order; at most 64 nearest-by-ID eligible connections participate.
inline void applyWaterCascadeFlow(const World& world,Vec3 position,
                                  WaterSample& sample) {
    if(!sample.found || !sample.submerged || sample.body==kNullEntity ||
       world.countExisting<WaterCascadeFlowPolicyComponent>()==0)
        return;
    constexpr size_t kMaxContributions=64;
    std::array<WaterCascadeFlowContribution,kMaxContributions> contributions{};
    size_t used=0;
    world.eachExistingConst<WaterCascadeFlowPolicyComponent>(
        [&](Entity e,const WaterCascadeFlowPolicyComponent& policy) {
            if(!world.alive(e) || !validWaterCascadeFlowPolicy(policy))
                return;
            const auto* links=world.tryGetConst<WaterCascadeConnectionsComponent>(e);
            const auto* cascade=world.tryGetConst<WaterCascadeComponent>(e);
            if(!links || !links->enabled || !cascade ||
               !validWaterCascade(*cascade))return;
            const bool atOutlet=sample.body==links->outlet;
            const bool atInlet=sample.body==links->inlet && !atOutlet;
            if(!atOutlet && !atInlet)return;
            Fixed upstreamSpeed{};
            if(atOutlet && links->inlet!=kNullEntity &&
               world.alive(links->inlet)) {
                const auto* inlet=world.tryGetConst<WaterBodyComponent>(links->inlet);
                if(inlet && validWaterBody(*inlet)) {
                    const auto* geometry=world.tryGetConst<WaterGeometryComponent>(links->inlet);
                    const auto* current=world.tryGetConst<WaterCurrentFieldComponent>(links->inlet);
                    const auto* vortices=world.tryGetConst<WaterVortexFieldComponent>(links->inlet);
                    const Vec3 upstream=waterCurrentWithVortices(
                        *inlet,geometry,current,vortices,
                        cascade->points.front().position);
                    upstreamSpeed=upstream.xz().length();
                }
            }
            const WaterCascadeFlowContribution contribution=
                waterCascadeMouthContribution(uint64_t(e.packed()),
                    *cascade,policy,atOutlet,position,upstreamSpeed);
            if(contribution.weight<=Fixed{})return;
            if(used<kMaxContributions) {
                contributions[used++]=contribution;
            }else{
                // Stable bounded selection even with different ECS pool
                // orders: retain the 64 lowest generation-safe IDs.
                size_t largest=0;
                for(size_t i=1;i<used;++i)
                    if(contributions[i].cascadeId>
                       contributions[largest].cascadeId)largest=i;
                if(contribution.cascadeId<contributions[largest].cascadeId)
                    contributions[largest]=contribution;
            }
        });
    sample.flow=blendWaterCascadeContributions(
        sample.flow,contributions,used);
}
} // namespace aiverse::lib::nereid
