// Optional Box3D-native half of the TerraWeave/NEREID example.
// No game client has to enable or link Box3D merely to use the
// deterministic terraweave_native/Integration.h ECS soil/water systems.
#pragma once
#include "examples/world/terraweave_native/Integration.h"
#include "engine/world/TerraWeaveNereidObstacleBridge.h"
#include "engine/world/TerraWeaveCavernsPlanetNativeHost.h"
#include <functional>
#include <span>
#include <set>
#include <stdexcept>
#include <utility>

namespace aiverse::examples::terraweave_native {

struct SlopePhysicsEvents {
    uint32_t debrisBodiesCreated=0;
    uint32_t fallenPlantsCreated=0;
};

// Call AFTER the authoritative ECS soil transaction and BEFORE the next
// physics::Scene::step(), not from an ECS worker or renderer callback.
// Soil already moved in Earthworks; debris bodies are bounded transient
// physical/presentation objects, not extra authoritative soil mass.
inline SlopePhysicsEvents applySlideAndTreePhysics(
        const World& ecs,
        const terraweave::TerraWeaveEcsAuthorityConfig& config,
        const terraweave::SlopeFailurePlan& slides,
        std::span<const terraweave::PlantFallEvent> fallen,
        uint64_t completedTick,
        terraweave::SlopeDebrisPhysics& debris,
        terraweave::FallenPlantPhysics& trees,
        terraweave::TerraWeaveForestScene* forest=nullptr,
        std::function<terraweave::FallenPlantBodySpec(
            const terraweave::PlantFallEvent&)> specForPlant={}) {
    const auto* resource=ecs.resourceConst<
        terraweave::TerraWeaveAuthorityResource>();
    if(!resource)
        throw std::invalid_argument(
            "TerraWeave missing canonical ECS soil for Box3D");
    if(fallen.size()>65536u)
        throw std::length_error("TerraWeave fallen plant event budget");
    for(const auto& event:fallen)
        if(event.tick!=completedTick)
            throw std::invalid_argument(
                "TerraWeave stale tree-fall physics event");
    auto earth=terraweave::materializeEcsTerrain(*resource,config);
    SlopePhysicsEvents result;
    result.debrisBodiesCreated=debris.spawn(
        slides,earth,completedTick);
    for(const auto& event:fallen){
        if(trees.body(event.id)!=physics::kNoBody)continue;
        auto spec=specForPlant?
            specForPlant(event):terraweave::FallenPlantBodySpec{};
        if(!specForPlant && event.habit==terraweave::PlantHabit::Shrub){
            spec.heightCm=140;
            spec.radiusCm=8;
            spec.angularSpeed=0.5f;
        }
        const auto body=trees.spawn(event,spec,forest);
        if(body!=physics::kNoBody)
            ++result.fallenPlantsCreated;
    }
    return result;
}

// Call after the completed Stage67 ECS decay tick on the Box3D owner
// thread. Fully rotten deadwood cannot remain a solid collidable log. The
// physics owner should then recapture the existing FallenNavigationSnapshot,
// mark changed rectangles dirty and refresh NEREID flow obstructions.
inline size_t removeFullyDecayedFallenBodies(
        const terraweave::ForestDeadwoodDecayOutcome& decay,
        terraweave::FallenPlantPhysics& falling) {
    if(decay.fullyDecayed.size()>1024u)
        throw std::length_error("TerraWeave deadwood cleanup budget");
    std::set<terraweave::AnchoredPlantId> unique;
    for(const auto& id:decay.fullyDecayed)
        if(!id.key || !id.speciesId || !unique.insert(id).second)
            throw std::invalid_argument("TerraWeave duplicate decay cleanup");
    size_t removed=0;
    for(const auto& id:unique)
        if(falling.erase(id,nullptr,false))++removed;
    return removed;
}

// After authoritative Box3D::step(), before NEXT NEREID ECS water sample.
// The host maps each generation-safe ECS water-body Entity to its stable
// NereidSceneSurface.id; never assume Entity.index == scene surface ID.
// Updates NEREID ECS current obstacles AND visual-scene whitewater/ripples.
inline terraweave::FlowObstructionPublishResult
publishObstructionsAfterPhysics(
        World& ecs,world::WorldScene& visualScene,
        const physics::Scene& physicsScene,
        const terraweave::FallenPlantPhysics* trees,
        const terraweave::SlopeDebrisPhysics* debris,
        const terraweave::NereidSceneWaterId& stableWaterIdForEcs,
        terraweave::FlowObstructionCapturePolicy policy={}) {
    return terraweave::synchronizeNereidPhysicalObstacles(
        ecs,visualScene,physicsScene,trees,debris,
        stableWaterIdForEcs,policy);
}

// Use in the project's existing native motor system, not as a second ECS
// system. The project owns proxy state, real exterior motor/coverage and
// the position component. The Cave host handles the exclusive dispatch.
template<class Target>
inline terraweave::caverns::CavePlanetDispatchResult dispatchPlanetCaveActor(
        terraweave::caverns::CavePlanetNativeHost<Target>& host,Simulation& sim,Entity entity,
        const physics::CharacterParams& params,physics::CharacterState& state,
        physics::BodyHandle proxy,const terraweave::caverns::CavePlanetMotorDispatchInput& input){
    return host.dispatchMotor(sim,entity,params,state,proxy,input);
}
// Optional owner-loop tick: the module already has one registered motor
// using dispatchPlanetCaveActor. Do not also call sim.step()/scene.step().
template<class Target>
inline auto stepPlanetCaveTick(terraweave::caverns::CavePlanetNativeHost<Target>& host,
        const std::set<uint64_t>& extraPins={}){
    return host.stepNativeTick(extraPins);
}
} // namespace aiverse::examples::terraweave_native
