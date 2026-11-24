// NEREID Stage 41 — local deterministic confluence current mixing.
// Reused by both reference and indexed water query paths. No genre/renderer
// dependencies; analytical read-only flow, not conservative hydraulics.
#pragma once
#include "NereidConfluences.h"
#include "NereidVortices.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace aiverse::lib::nereid {

struct WaterConfluenceFlowContribution {
    uint64_t confluenceId=0;
    Vec2 desired{};
    Fixed weight{};
    Fixed speedLimit=1000_fx; // applied only for retained contributors
};

// Shared math kernel is also available to future immutable scene adapters.
// The caller resolves/sorts upstream sources and supplies their mean X/Z flow.
inline WaterConfluenceFlowContribution waterConfluenceMouthContribution(
        uint64_t id,const WaterConfluenceComponent& junction,
        Vec2 upstreamMean,Vec3 position) {
    WaterConfluenceFlowContribution out;
    if(!validWaterConfluenceFlowPolicy(junction))return out;
    const Fixed distance=(position.xz()-junction.mouth.xz()).length();
    if(distance>=junction.radius)return out;
    const Fixed t=fxClamp(1_fx-distance/junction.radius,0_fx,1_fx);
    const Fixed weight=junction.blend*t*t*(3_fx-2_fx*t);
    if(weight<=Fixed{})return out;
    return {id,upstreamMean,weight,junction.speedLimit};
}

template<size_t N>
inline Vec3 blendWaterConfluenceContributions(
        Vec3 base,std::array<WaterConfluenceFlowContribution,N>& entries,
        size_t count) {
    count=std::min(count,N);
    if(!count)return base;
    std::sort(entries.begin(),entries.begin()+count,
        [](const auto& a,const auto& b){
            return a.confluenceId<b.confluenceId;
        });
    Vec2 weighted{};
    Fixed weightSum{};
    Fixed clampLimit=1000_fx;
    // A discarded higher-ID junction must not constrain flow from
    // the 32 retained, lowest-ID eligible contributions.
    for(size_t i=0;i<count;++i) {
        weighted+=entries[i].desired*entries[i].weight;
        weightSum+=entries[i].weight;
        clampLimit=fxMin(clampLimit,entries[i].speedLimit);
    }
    if(weightSum<=Fixed{})return base;
    const Vec2 desired=weighted/weightSum;
    const Fixed alpha=fxClamp(weightSum,0_fx,1_fx);
    const Vec2 mixed=base.xz()*(1_fx-alpha)+desired*alpha;
    base.x=fxClamp(mixed.x,-clampLimit,clampLimit);
    base.z=fxClamp(mixed.y,-clampLimit,clampLimit);
    return base;
}

// Stable and bounded: sort inlet generation-safe IDs before averaging and
// keep the 32 lowest eligible confluence IDs regardless of ECS pool order.
// Invalid/missing linked inputs are ignored as a WHOLE junction, never
// partially mixed; no body/contact/current components are written here.
inline void applyWaterConfluenceFlow(const World& world,Vec3 position,
                                     WaterSample& sample) {
    if(!sample.found || !sample.submerged || sample.body==kNullEntity ||
       world.countExisting<WaterConfluenceComponent>()==0)return;
    constexpr size_t kMaxContributions=32;
    std::array<WaterConfluenceFlowContribution,kMaxContributions> entries{};
    size_t used=0;
    world.eachExistingConst<WaterConfluenceComponent>(
        [&](Entity e,const WaterConfluenceComponent& junction) {
            if(!world.alive(e) || !validWaterConfluence(junction) ||
               junction.outlet!=sample.body)return;
            const auto* outlet=world.tryGetConst<WaterBodyComponent>(
                junction.outlet);
            if(!outlet || !validWaterBody(*outlet) ||
               !coversWaterBody(*outlet,
                   world.tryGetConst<WaterGeometryComponent>(junction.outlet),
                   junction.mouth))return;
            // The same authored mouth must touch each upstream footprint.
            auto sorted=junction.inlets;
            std::sort(sorted.begin(),sorted.begin()+junction.inletCount,
                [](Entity a,Entity b){return a.packed()<b.packed();});
            Vec2 sum{};
            for(size_t i=0;i<junction.inletCount;++i) {
                const Entity inlet=sorted[i];
                if(!world.alive(inlet))return;
                const auto* body=world.tryGetConst<WaterBodyComponent>(inlet);
                const auto* geometry=
                    world.tryGetConst<WaterGeometryComponent>(inlet);
                if(!body || !validWaterBody(*body) ||
                   !coversWaterBody(*body,geometry,junction.mouth))return;
                const Vec3 current=waterCurrentWithVortices(
                    *body,geometry,
                    world.tryGetConst<WaterCurrentFieldComponent>(inlet),
                    world.tryGetConst<WaterVortexFieldComponent>(inlet),
                    junction.mouth);
                sum.x+=fxClamp(current.x,-1000_fx,1000_fx);
                sum.y+=fxClamp(current.z,-1000_fx,1000_fx);
            }
            const Fixed count=Fixed::fromInt(junction.inletCount);
            const auto contribution=waterConfluenceMouthContribution(
                uint64_t(e.packed()),junction,sum/count,position);
            if(contribution.weight<=Fixed{})return;
            if(used<kMaxContributions)entries[used++]=contribution;
            else {
                size_t largest=0;
                for(size_t i=1;i<used;++i)
                    if(entries[i].confluenceId>entries[largest].confluenceId)
                        largest=i;
                if(contribution.confluenceId<entries[largest].confluenceId)
                    entries[largest]=contribution;
            }
        });
    sample.flow=blendWaterConfluenceContributions(
        sample.flow,entries,used);
}
} // namespace aiverse::lib::nereid
