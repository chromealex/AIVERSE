// NEREID Stage 62: opt-in authoritative local current deflection by fallen
// trees, debris or other solid obstructions. WaterBody/river/vortex/confluence
// remain the SINGLE water source: this modifies the resolved WaterSample.flow
// only after NEREID has selected the water body and applied mouth policies.
// Fixed-point, stable entity ordering, no GPU/Box3D or second water solver.
#pragma once
#include <aiverselib/water/NereidWater.h>
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

// Authored/host-derived ECS obstacle. Width is an XZ radius, and the vertical
// interval must actually intersect the selected water body water column.
// Water geometry and body currents are NEVER mutated. sourceKey=0 is valid
// for a manually authored obstacle; generated sources use stable nonzero ids.
struct WaterFlowObstructionComponent {
    Entity waterBody=kNullEntity;
    Vec2 center{};
    Fixed radius=1_fx;
    Fixed bottomY=-2_fx,topY=2_fx;
    uint32_t permeabilityPermille=250; // 0 solid, 1000 no slowdown
    uint32_t deflectionPermille=500;   // 0 straight slowdown, 1000 sideways
    uint64_t sourceKey=0;
    bool enabled=true;
};
inline constexpr size_t kMaxWaterFlowObstructions=256;

inline bool validWaterFlowObstruction(
        const WaterFlowObstructionComponent& o){
    return o.waterBody.valid() && o.radius>=Fixed::fromRatio(1,20) &&
        o.radius<=100_fx && o.bottomY<o.topY &&
        o.permeabilityPermille<=1000u && o.deflectionPermille<=1000u &&
        fxAbs(o.center.x)<=100000_fx &&
        fxAbs(o.center.y)<=100000_fx &&
        fxAbs(o.bottomY)<=100000_fx && fxAbs(o.topY)<=100000_fx;
}

// Obstacle occupies a bounded horizontal footprint. Sideways motion follows
// the flow direction and on which side of the blockage the point lies. The
// centreline tie is resolved by stable source id to avoid nondeterministic
// local flow at the middle of a fallen trunk.
inline Vec3 deflectNereidFlow(
        Vec3 incoming,const WaterFlowObstructionComponent& obstruction,
        Vec3 sampleAt,uint64_t stableEntityId=0) {
    if(!obstruction.enabled || !validWaterFlowObstruction(obstruction))
        return incoming;
    const Fixed dx=sampleAt.x-obstruction.center.x;
    const Fixed dz=sampleAt.z-obstruction.center.y;
    const Fixed offset=fxMax(fxAbs(dx),fxAbs(dz));
    if(offset>=obstruction.radius)return incoming;
    const Fixed influence=(obstruction.radius-offset)/obstruction.radius;
    const Fixed slowing=Fixed::fromRatio(
        1000u-obstruction.permeabilityPermille,1000)*influence;
    if(slowing<=0_fx)return incoming;
    const Fixed cross=incoming.x*dz-incoming.z*dx;
    // A generated sourceKey is preserved across ECS entity recycling and
    // copied into the visual-scene snapshot. Manual obstacles fall back to
    // the authoritative generation-safe ECS entity ID.
    const uint64_t tieId=obstruction.sourceKey ?
        obstruction.sourceKey:stableEntityId;
    const bool positive=cross>0_fx ||
        (cross==0_fx && (tieId&1u));
    const Fixed side=positive?1_fx:-1_fx;
    const Fixed diversion=Fixed::fromRatio(
        obstruction.deflectionPermille,1000)*slowing;
    // Rotate a bounded fraction of the removed forward component sideways,
    // instead of magically increasing current energy at the blockage.
    const Fixed remain=1_fx-slowing;
    return {
        fxClamp(incoming.x*remain-incoming.z*diversion*side,
                -1000_fx,1000_fx),
        incoming.y,
        fxClamp(incoming.z*remain+incoming.x*diversion*side,
                -1000_fx,1000_fx)
    };
}

inline void applyNereidFlowObstructions(
        const World& world,Vec3 sampleAt,WaterSample& sample){
    if(!sample.found || !sample.body.valid() ||
       world.countExisting<WaterFlowObstructionComponent>()==0)
        return;
    // Do not trust ECS pool insertion order: sorted by generation-safe
    // entity identity, identical in reference/indexed/adaptive water queries.
    struct Candidate { // trivial: only [0,count) is ever read
        uint64_t order,ecsId;
        const WaterFlowObstructionComponent* obstacle;
    };
    // Bounded stack scratch (no heap allocation per water probe).
    std::array<Candidate,kMaxWaterFlowObstructions> candidates;
    size_t count=0;
    world.eachExistingConst<WaterFlowObstructionComponent>(
        [&](Entity id,const WaterFlowObstructionComponent& o){
            if(!world.alive(id) || !o.enabled)return;
            if(!validWaterFlowObstruction(o))
                throw std::invalid_argument("NEREID invalid active current obstruction");
            if(o.waterBody!=sample.body ||
               o.topY<=sample.bottomY || o.bottomY>=sample.surfaceY)
                return;
            if(count>=kMaxWaterFlowObstructions)
                throw std::length_error("NEREID active flow obstruction budget");
            // Generated sources are sorted by stable source ID on BOTH ECS
            // and visual side, so overlapping logs/boulders deflect water in
            // the same sequence despite ECS entity recycling. Authored
            // obstacles use their generation-safe entity ID as a fallback.
            candidates[count++]={
                o.sourceKey?o.sourceKey:id.packed(),id.packed(),&o};
        });
    std::sort(candidates.begin(),candidates.begin()+std::ptrdiff_t(count),
        [](const auto& a,const auto& b){
            return a.order==b.order ?
                a.ecsId<b.ecsId : a.order<b.order;
        });
    for(size_t i=0;i<count;++i){
        const auto& entry=candidates[i];
        sample.flow=deflectNereidFlow(
            sample.flow,*entry.obstacle,sampleAt,entry.ecsId);
    }
}

inline void io(Archive& a,WaterFlowObstructionComponent& o){
    a(o.waterBody,o.center.x,o.center.y,o.radius,
      o.bottomY,o.topY,o.permeabilityPermille,
      o.deflectionPermille,o.sourceKey,o.enabled);
    if(!a.writing() && o.enabled && !validWaterFlowObstruction(o))
        a.fail();
}
inline void registerNereidFlowObstructionPersistence(Simulation& sim){
    sim.registerPersistentComponent<WaterFlowObstructionComponent>(
        "nereid.flow_obstruction",1);
}

} // namespace aiverse::lib::nereid
