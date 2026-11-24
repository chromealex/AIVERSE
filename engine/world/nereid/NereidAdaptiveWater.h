// NEREID stage 6 -- deterministic adaptive water sampling.
// Tick policy uses ECS counts, never wall time or FPS (lockstep safe).
#pragma once
#include "NereidQueryIndex.h"
#include <cstddef>
#include <string>
#include <utility>

namespace aiverse::lib::nereid {
struct AdaptiveWaterSamplingConfig {
    size_t minWaterBodies=16;
    size_t minProbes=8;
};
inline bool chooseIndexedWaterSampling(size_t bodies,size_t probes,
                                       AdaptiveWaterSamplingConfig config={}) {
    return bodies>0 && probes>0 &&
           bodies>=config.minWaterBodies && probes>=config.minProbes;
}
// Install ONE of reference/indexed/adaptive systems. All write the same
// persisted WaterContactComponent without altering ECS water-body ownership.
inline void addAdaptiveWaterSamplingSystem(Simulation& sim,std::string name,
                                            AdaptiveWaterSamplingConfig config={}) {
    sim.addSystem(std::move(name),[config](Simulation& current) {
        World& world=current.world();
        if(!world.countExisting<WaterContactComponent>())return;
        // Detach shared ECS pool before writing, preserving COW rollback.
        auto* contacts=&world.pool<WaterContactComponent>();
        const World& read=world;
        const bool indexed=chooseIndexedWaterSampling(
            read.countExisting<WaterBodyComponent>(),contacts->size(),config);
        if(indexed) {
            const WaterQueryIndex index(read);
            for(size_t i=0;i<contacts->size();++i) {
                const Entity entity=contacts->entityAt(i);
                if(!read.alive(entity))continue;
                const auto* probe=read.tryGetConst<WaterProbeComponent>(entity);
                auto& contact=contacts->dataAt(i);
                contact.sample=probe?index.sample(probe->position):WaterSample{};
                contact.sampledTick=current.tick();
            }
        }else{
            for(size_t i=0;i<contacts->size();++i) {
                const Entity entity=contacts->entityAt(i);
                if(!read.alive(entity))continue;
                const auto* probe=read.tryGetConst<WaterProbeComponent>(entity);
                auto& contact=contacts->dataAt(i);
                contact.sample=probe?sampleWater(read,probe->position):WaterSample{};
                contact.sampledTick=current.tick();
            }
        }
    });
}
} // namespace aiverse::lib::nereid
