// AIVERSE NEREID stage 2 -- shared fixed-point buoyancy/drag force policy.
// Force output is consumed by the host physics/movement system; NEREID does
// not duplicate position, velocity, collider or integrator ownership.
#pragma once
#include "NereidWaterSystems.h"
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

// Acceleration includes buoyancy and drag but NOT ordinary gravity.
// The host keeps its canonical velocity and supplies a per-tick input.
struct WaterBuoyancyComponent {
    Fixed buoyancyAcceleration=20_fx; // m/s^2 at full immersion
    Fixed linearDrag=2_fx;            // 1/s; accelerates toward water flow
    Fixed immersionHeight=2_fx;       // metres for full immersion
    bool enabled=true;
};
struct WaterVelocityInputComponent {Vec3 velocity{};};
struct WaterForceComponent {
    Vec3 acceleration{};
    Fixed immersedFraction{};
    uint32_t sampledTick=0;
};

inline bool validBuoyancy(const WaterBuoyancyComponent& b) {
    return b.enabled && b.immersionHeight>Fixed{} &&
           b.immersionHeight<=10000_fx &&
           b.buoyancyAcceleration>=Fixed{} &&
           b.buoyancyAcceleration<=1000_fx &&
           b.linearDrag>=Fixed{} && b.linearDrag<=1000_fx;
}

// Pure, deterministic: no fluid GPU readback, game genres or backend physics.
// Forces are 0 if the probe is outside or above the water surface.
inline WaterForceComponent computeWaterForces(const WaterSample& sample,
                                              const WaterBuoyancyComponent& params,
                                              Vec3 velocity) {
    WaterForceComponent out;
    if(!sample.found || !sample.submerged || !validBuoyancy(params))return out;
    out.immersedFraction=fxClamp(sample.depth/params.immersionHeight,
                                Fixed{},1_fx);
    const Vec3 relative=velocity-sample.flow;
    out.acceleration=Vec3{Fixed{},params.buoyancyAcceleration*out.immersedFraction,
                          Fixed{}} +
                     relative*(-params.linearDrag*out.immersedFraction);
    return out;
}

// Stage order: input/physics -> water probe -> water contact -> buoyancy force
// -> host physics integration. Missing inputs or stale contacts produce zero.
inline void addWaterBuoyancySystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* forces=world.prepareExistingWrite<WaterForceComponent>();
        if(!forces)return;
        const World& read=world;
        for(size_t i=0;i<forces->size();++i) {
            const Entity entity=forces->entityAt(i);
            if(!world.alive(entity))continue;
            const auto* sample=read.tryGetConst<WaterContactComponent>(entity);
            const auto* params=read.tryGetConst<WaterBuoyancyComponent>(entity);
            const auto* velocity=read.tryGetConst<WaterVelocityInputComponent>(entity);
            auto& force=forces->dataAt(i);
            force=sample && params && velocity && sample->sampledTick==current.tick()
                ?computeWaterForces(sample->sample,*params,velocity->velocity)
                :WaterForceComponent{};
            force.sampledTick=current.tick();
        }
    });
}

inline void registerNereidBuoyancyPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterBuoyancyComponent>("nereid.buoyancy",1);
    sim.registerPersistentComponent<WaterVelocityInputComponent>("nereid.velocity_input",1);
    sim.registerPersistentComponent<WaterForceComponent>("nereid.force",1);
}

inline void io(Archive& a,WaterBuoyancyComponent& b) {
    a(b.buoyancyAcceleration,b.linearDrag,b.immersionHeight,b.enabled);
}
inline void io(Archive& a,WaterVelocityInputComponent& b) {a(b.velocity);}
inline void io(Archive& a,WaterForceComponent& f) {
    a(f.acceleration,f.immersedFraction,f.sampledTick);
}

} // namespace aiverse::lib::nereid
