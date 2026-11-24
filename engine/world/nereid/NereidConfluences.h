// NEREID Stage 41 — explicit linked confluence policy for genre-neutral water.
// This ECS state is optional and never integrates fluid mass or host physics.
#pragma once
#include "NereidWater.h"
#include "engine/sim/Simulation.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace aiverse::lib::nereid {
inline constexpr size_t kMaxWaterConfluenceInlets=4;

// The host creates a separate junction Entity and supplies generation-safe
// water-body IDs. The mouth must lie within all input footprints and outlet.
// Current is blended locally near the outlet; body flow is never modified.
struct WaterConfluenceComponent {
    std::array<Entity,kMaxWaterConfluenceInlets> inlets{
        kNullEntity,kNullEntity,kNullEntity,kNullEntity};
    uint8_t inletCount=0;
    Entity outlet=kNullEntity;
    Vec3 mouth{};
    Fixed radius=8_fx;                // influence radius (metres X/Z)
    Fixed blend=Fixed::fromRatio(3,4); // 0..1
    Fixed speedLimit=30_fx;           // final X/Z clamp, 0..1000 m/s
    bool enabled=true;
};

// Flow policy has no dependency on Entity encoding. Presentation manifests
// use stable scene IDs and must never invent fake ECS Entity handles.
inline bool validWaterConfluenceFlowPolicy(const WaterConfluenceComponent& c) {
    return c.enabled && c.inletCount>=2 &&
           c.inletCount<=kMaxWaterConfluenceInlets &&
           c.radius>=Fixed::fromRatio(1,10) && c.radius<=1000_fx &&
           c.blend>=0_fx && c.blend<=1_fx &&
           c.speedLimit>0_fx && c.speedLimit<=1000_fx;
}
inline bool validWaterConfluence(const WaterConfluenceComponent& c) {
    if(!validWaterConfluenceFlowPolicy(c) || c.outlet==kNullEntity)
        return false;
    for(size_t i=0;i<c.inletCount;++i) {
        if(c.inlets[i]==kNullEntity || c.inlets[i]==c.outlet)return false;
        for(size_t j=0;j<i;++j)if(c.inlets[i]==c.inlets[j])return false;
    }
    return true;
}

// Field-wise schema with a fixed four-slot payload, bounded regardless of
// component construction order or active inlet count.
inline void io(Archive& a,WaterConfluenceComponent& c) {
    a(c.inletCount,c.outlet,c.mouth,c.radius,c.blend,c.speedLimit,c.enabled);
    for(auto& inlet:c.inlets)a(inlet);
    if(c.inletCount>kMaxWaterConfluenceInlets)a.fail();
}
inline void registerNereidConfluencePersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterConfluenceComponent>(
        "nereid.confluence",1);
}
} // namespace aiverse::lib::nereid
