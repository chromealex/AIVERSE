// AIVERSE NEREID -- opt-in ECS sampling; no FPS/RTS or rendering coupling.
#pragma once
#include "NereidCascadeFlow.h"
#include "NereidRiverGrade.h"
#include "NereidConfluenceFlow.h"
#include "NereidFlowObstacles.h"
#include "NereidFloodRouting.h"
#include "engine/sim/CommonComponents.h"
#include "engine/sim/Simulation.h"
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

// Selection is independent of ECS pool insertion order. Higher priority wins,
// then higher surface, then smallest generation-safe Entity ID. No RNG / floats.
inline void considerWaterCandidate(WaterSample& sample,Entity entity,
                                   const WaterBodyComponent& sourceBody,Vec3 position,
                                   const WaterGeometryComponent* geometry=nullptr,
                                   const WaterCurrentFieldComponent* field=nullptr,
                                   const WaterVortexFieldComponent* vortices=nullptr,
                                   const WaterRiverGradeComponent* grade=nullptr) {
    const auto body=gradedWaterBody(sourceBody,grade,position,geometry);
    const bool better=!sample.found ||
        body.priority>sample.priority ||
        (body.priority==sample.priority &&
         (body.surfaceY>sample.surfaceY ||
          (body.surfaceY==sample.surfaceY && entity.packed()<sample.body.packed())));
    if(!better)return;
    sample.body=entity;
    sample.kind=body.kind;
    sample.surfaceY=body.surfaceY;
    sample.bottomY=body.bottomY;
    sample.flow=waterCurrentWithVortices(body,geometry,field,vortices,position);
    sample.priority=body.priority;
    sample.depth=fxClamp(body.surfaceY-position.y,Fixed{},body.surfaceY-body.bottomY);
    sample.found=true;
    sample.submerged=position.y<body.surfaceY && position.y>=body.bottomY;
}
inline WaterSample sampleWater(const World& world,Vec3 position) {
    WaterSample sample;
    world.eachExistingConst<WaterBodyComponent>(
        [&](Entity entity,const WaterBodyComponent& body) {
            if(!world.alive(entity))return;
            const auto* geometry=world.tryGetConst<WaterGeometryComponent>(entity);
            if(!coversWaterBody(body,geometry,position))return;
            const auto* field=world.tryGetConst<WaterCurrentFieldComponent>(entity);
            const auto* vortices=world.tryGetConst<WaterVortexFieldComponent>(entity);
            considerWaterCandidate(sample,entity,body,position,geometry,field,vortices,world.tryGetConst<WaterRiverGradeComponent>(entity));
        });
    // Confluences blend upstream rivers first; cascade mouth continuity
    // runs last so drops take local precedence without changing body flow.
    applyWaterConfluenceFlow(world,position,sample);
    applyWaterCascadeFlow(world,position,sample);
    applyNereidFlowObstructions(world,position,sample);
    return applyNereidFloodSample(
        world,world.resourceConst<WaterFloodResource>(),position,sample);
}

// Optional adapter to the existing shared world Transform, after locomotion/
// integration and before water sampling. Projects may supply probes otherwise.
inline void addWaterProbeFromTransformSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        if(!world.countExisting<WaterProbeComponent>())return;
        // Detach the shared COW pool before publishing probe positions.
        auto* probes=&world.pool<WaterProbeComponent>();
        const World& read=world;
        for(size_t i=0;i<probes->size();++i) {
            const Entity e=probes->entityAt(i);
            if(!world.alive(e))continue;
            if(const auto* t=read.tryGetConst<Transform>(e))
                probes->dataAt(i).position=t->pos;
        }
    });
}

// Read: WaterBody + WaterProbe. Write: WaterContact only. No structural changes.
// Complexity O(bodies * probes); broadphase is a future NEREID stage.
inline void addWaterSamplingSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        if(!world.countExisting<WaterContactComponent>())return;
        // Detach the shared COW pool before modifying contact samples.
        auto* contacts=&world.pool<WaterContactComponent>();
        const World& read=world;
        for(size_t i=0;i<contacts->size();++i) {
            const Entity e=contacts->entityAt(i);
            if(!world.alive(e))continue;
            const auto* probe=read.tryGetConst<WaterProbeComponent>(e);
            auto& contact=contacts->dataAt(i);
            contact.sample=probe?sampleWater(read,probe->position):WaterSample{};
            contact.sampledTick=current.tick();
        }
    });
}

inline void registerNereidVortexPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterVortexFieldComponent>("nereid.vortex_field",1);
}
inline void registerNereidRiverGradePersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterRiverGradeComponent>("nereid.river_grade",1);
}
inline void registerNereidCurrentPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterCurrentFieldComponent>("nereid.current_field",1);
}

// Native-project integration; registered persistence includes configuration,
// query position AND sampled gameplay data for save/replay/checksum.
inline void registerNereidWaterPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterBodyComponent>("nereid.water_body",1);
    sim.registerPersistentComponent<WaterProbeComponent>("nereid.water_probe",1);
    sim.registerPersistentComponent<WaterContactComponent>("nereid.water_contact",1);
}

// Optional schema: register when any water body uses polygon/spline geometry.
inline void registerNereidGeometryPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterGeometryComponent>("nereid.geometry",1);
}

} // namespace aiverse::lib::nereid
