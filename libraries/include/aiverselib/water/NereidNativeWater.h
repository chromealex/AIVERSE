// NEREID stage 6 -- opt-in native-game water pipeline.
// Called from GameModule::setup, never from a simulation tick.
#pragma once
#include <aiverselib/water/NereidAdaptiveWater.h>
#include <aiverselib/water/NereidBuoyancySystems.h>
#include <aiverselib/water/NereidInteractions.h>
#include <aiverselib/water/NereidCascades.h>
#include <aiverselib/water/NereidConfluences.h>
#include <aiverselib/water/NereidShorelineSystems.h>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct NativeWaterPipelineConfig {
    bool enableGeometry=false;
    bool enableBuoyancy=false;
    bool enableInteractions=false;
    bool enableCascades=false;
    bool enableConfluences=false;
    bool enableCurrentField=false;
    bool enableVortices=false;
    bool enableBankErosion=false;
    bool enableFlowObstructions=false;
    bool enableFloodRouting=false;
    uint32_t floodTickMs=16;
    WaterFloodSettings floodSettings{};
    uint32_t bankErosionTickMs=16;
    bool probeFromTransform=true;
    AdaptiveWaterSamplingConfig adaptive{};
    bool enableRiverGrade=false;
};

// Register the ECS components and ordered systems once during native setup.
// The host still owns gravity, integrator and water-body entity lifecycle.
inline void registerNativeWaterPipeline(Simulation& sim,
        std::string prefix="nereid",NativeWaterPipelineConfig config={}) {
    registerNereidWaterPersistence(sim);
    if(config.enableRiverGrade)registerNereidRiverGradePersistence(sim);
    if(config.enableGeometry)registerNereidGeometryPersistence(sim);
    if(config.enableBuoyancy)registerNereidBuoyancyPersistence(sim);
    if(config.enableInteractions)registerNereidInteractionPersistence(sim);
    if(config.enableCascades)registerNereidCascadePersistence(sim);
    if(config.enableConfluences)registerNereidConfluencePersistence(sim);
    if(config.enableCurrentField)registerNereidCurrentPersistence(sim);
    if(config.enableVortices)registerNereidVortexPersistence(sim);
    if(config.enableBankErosion)registerNereidBankErosionPersistence(sim);
    if(config.enableFlowObstructions)
        registerNereidFlowObstructionPersistence(sim);
    if(config.enableFloodRouting)
        registerNereidFloodPersistence(sim);
    if(config.probeFromTransform)
        addWaterProbeFromTransformSystem(sim,prefix+".probe");
    // Flood volume routing precedes water probes, bank erosion and
    // sediment advection; no second water body system is registered.
    if(config.enableFloodRouting)
        addNereidFloodRoutingSystem(sim,prefix+".flood",
                                   config.floodTickMs,config.floodSettings);
    addAdaptiveWaterSamplingSystem(sim,prefix+".sample",config.adaptive);
    if(config.enableBankErosion)
        addNereidBankErosionSystem(sim,prefix+".bank_erosion",
                                  config.bankErosionTickMs);
    if(config.enableBuoyancy)
        addWaterBuoyancySystem(sim,prefix+".buoyancy");
    if(config.enableInteractions)
        addWaterInteractionSystem(sim,prefix+".interaction");
}

// Deterministic body entity creation order, same across independent hosts.
// Call at setup or a canonical structural-write barrier; no implicit legacy
// map mutation, resource mirroring, networking or editor dependency.
inline std::vector<Entity> spawnNativeWaterBodies(
        World& world,const std::vector<WaterBodyComponent>& bodies) {
    std::vector<Entity> result;
    result.reserve(bodies.size());
    for(const auto& body:bodies) {
        const Entity e=world.create();
        world.add<WaterBodyComponent>(e,body);
        result.push_back(e);
    }
    return result;
}
} // namespace aiverse::lib::nereid
