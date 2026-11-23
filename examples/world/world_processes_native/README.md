# AIVERSE — MORPH + GRANULAR physical native host

This is an **opt-in** integration layer above the engine. It reuses the
canonical ECS World, Box3D Scene, Renderer3D, GPU Device, and TerraWeave soil
authority. It does not implement another physics or renderer.

## Native files and actual APIs

| Header | Purpose |
|---|---|
| MorphModelStream.h | Morph real imported ModelVertex data per visual instance through the existing full vertexOverride path |
| MorphBox3DContactBridge.h | Quantize real Box3D begin contacts into replayable one-shot MORPH Impact inputs |
| GranularTerraWeaveBinding.h | Bake a 2D granular field into render MeshBuilder slabs and matching Box3D static top triangles, sampling TerraWeave EarthworksLayer |
| GranularWorldIntegration.h | Stage 16x16 resident tiles from one completed ECS+soil state and publish separately on Box3D and GPU threads |
| GranularTerraWeaveTransfer.h | Dig/Fill real TerraWeave authoritative ECS soil and debit/credit GRANULAR cell mass using exact movedVolumeCm3 |
| PhysicalSceneHost.h | Convenience host facade for the above lifecycle, rollback invalidation and shutdown |

The minimal module in examples/native/world_processes does **not** link
Box3D or Renderer3D. A native game that wants physical presentation must
also include this directory and link its own scene host target against
the existing render/model and optional AIVERSE::physics libraries.

## Example lifecycle (native project C++20)

~~~cpp
#include "examples/world/world_processes_native/PhysicalSceneHost.h"
using namespace aiverse;
using namespace aiverse::examples::world_processes_native;

// Your existing host owns the Simulation, gfx::Device, Renderer3D,
// physics::Scene, terrain ECS authority and source imported Model.
PhysicalSceneHost processes;

// Before uploadModel, set model.batching.allowGeometryPool=false.
// Retain original ModelVertex + index spans and map model-relative
// vertex indices to MORPH Entity IDs with 16-bit normalized weights.
processes.addMorphVisual(instanceId, device, model,
                         pristineVertices, sourceIndices, influences);

// AFTER authoritative Box3D scene.step() (not on other clients):
std::vector<MorphContactInput> contacts;
processes.collectMorphPhysicsInputs(scene, simulation.world(),
                                    rigidBodyToMorphNode,
                                    nextEcsTick, contacts);
// Record/replicate these integer inputs in the existing replay channel.
// BEFORE the matching MORPH tick:
processes.applyRecordedMorphInputs(simulation.world(), contacts, nextEcsTick);

// AFTER completed gameplay tick, with the matching TerraWeave soil revision:
auto frame=processes.prepareGranular(simulation.world(),
                                    earthworks, residentTiles,
                                    granularPlacements, simulation.tick());
// Box3D owner thread, between Scene::step() calls:
if (!processes.publishPhysics(scene, frame)) {
    // Gate game physics until publication is repaired.
}
// Graphics owner thread, between Renderer3D render frames:
if (!processes.publishRender(device, frame)) {
    // Keep last valid frame and retry.
}

// During the renderer's normal scene frame:
processes.updateMorphVisuals(simulation.world());
renderer.beginScene(camera, viewportWidth, viewportHeight);
render::ModelDraw draw;
draw.model=&model;
draw.world=modelTransform;
processes.attachMorphVisual(instanceId, draw);
renderer.drawModel(draw);
processes.drawGranular(renderer);
renderer.render(pass);

// Optional TerraWeave Dig/Fill (authoritative ECS World barrier):
GranularTerraTransfer transfer;
transfer.field=fieldEntity;
transfer.x=0;transfer.row=0;
transfer.mode=terraweave::EarthworkMode::Fill;
GranularTerraTransferResult output;
processes.transferTerrainMass(simulation.world(), terrainConfig,
                              transfer, simulation.tick(), output);

// After rollback/load to an earlier tick, rehydrate all derived patches:
processes.resetAfterRollback(scene,device);
terraBinding.reconcile(simulation.world(),worldScene,true);
// Then prepare/publish new GRANULAR tiles from the restored World.
// On scene shutdown, after last draw has been retired:
processes.shutdownAfterIdle(scene,device);
~~~

Variable names such as model, earthworks, residentTiles, camera, pass,
scene, device and simulation are intentionally supplied by the concrete
project host. Including these headers alone does not auto-create a Box3D
Scene or bind every game object.

## Invariants

- Box3D is the only rigid solver. MORPH vertex skinning updates the real
  Renderer3D ModelDraw presentation stream. A coarse kinematic collider is
  available, but collision triangles do NOT automatically deform per vertex.
- MORPH uses packed generation-safe Entity references. Contacts are
  quantized on the authoritative server and recorded before they become
  one-shot ECS impacts. Never rerun float Box3D on each network client.
- GRANULAR field rows map to a local authored downstream (+Z) axis. They
  do not infer the full 3D terrain gradient or replace TerraWeave slope
  erosion. Existing Earthworks provides all ground heights.
- GRANULAR physics is a sparse static top-surface overlay above the real
  TerraWeave collider. Actual Dig/Fill edits use the existing authoritative
  TerraWeave ECS resource, not the renderer's height field.
- The host must gate simulation and presentation when a multi-tile batch
  partially fails to publish; every individual tile preserves its old
  collider on allocation failure.
- Separate renderer and Box3D lifetime fences matter. Recreate publishers
  after rollback to an earlier tick rather than reusing future revisions.
- Local prefab/game-specific visual/physics bindings and content import
  remain the host's responsibility. There is no engine-wide implicit opt-in.

## Verification status

At the user's request tests are deferred. These are committed source
integrations, **not** verified Mac/Metal, Vulkan, GLES or Box3D runtime
acceptance. No screenshots or passing CTest have been produced for the
Stage17+ physical integration. Complete handoffs:
docs/handoffs/world-processes-stage17-morph-render-box3d-2026-11-23.md
docs/handoffs/world-processes-stage18-granular-terra-box3d-2026-11-23.md
docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md


## CIRCUIT, EVOLVE and SOCIETY gameplay integration (Stage22–25)

Include the new optional native facade:

~~~cpp
#include "examples/world/world_processes_native/WorldProcessGameplayHost.h"
using namespace aiverse::examples::world_processes_native;
WorldProcessGameplayHost bindings;

// Connect a real existing Box3D kinematic door to its CIRCUIT Device Entity.
CircuitDoorBinding door;
door.body=realKinematicBodyHandle; // existing physics::Scene owner
door.closedPosition={0,0,0};
door.openPosition={0,4,0};
bindings.circuitDevices().bindDoor(doorDeviceEntity,door);

// Real project lighting/animation/pump/alarms and AI/quest owners supply
// idempotent callbacks; this module does NOT create synthetic game objects.
bindings.circuitDevices().setCallback(
    [&](const CircuitDeviceAction& event) {
        // Route event.device, event.kind and event.active to your existing
        // light/animation/pump/audio/trigger system.
        return applyExistingDeviceState(event);
    });
bindings.npcReactions().setDispatch(
    [&](const NpcKnowledgeAction& event) {
        // Route actionId, person, topics and transition to the existing
        // behavior tree / dialogue / quest graph owner.
        return dispatchExistingNpcEvent(event);
    });

// Player switch interaction at authoritative command barrier:
bindings.setSwitch(sim.world(), switchEntity, true);

// Optional PHYSIS source -> EVOLVE climate rates. Only after the PHYSIS and
// EVOLVE owner entities exist and before the fixed tick system runs:
evolve::ClimateLink climate;
climate.rainEnrichment=100;
climate.droughtConsumption=30;
bindings.attachClimate(sim.world(),climateLinkEntity,
                       regionEntity,physisCellEntity,climate);

// Wrap the existing TerraWeave forest climate callback with EVOLVE region
// effects (finite, world-centimetre bounds). It reuses the real ecology API.
auto forestClimate=bindings.forestClimate(
    sim.world(),
    {{regionEntity,-10000,-10000,10000,10000,true}},
    existingPhysisNereidClimateSampler);
// Pass forestClimate into TerraWeave's existing advanceForestEcology(...).

// AFTER the completed simulation tick: CIRCUIT outputs and SOCIETY trigger
// results were written into the authoritative ECS:
if(!bindings.reconcile(sim.world(),sim.tick(),&box3dScene))
    gateGameplayOutputsAndRetry();

// After rollback/load, reset external acknowledgements and reapply real
// state from the restored World:
bindings.resetAfterRollback();
bindings.reconcile(sim.world(),sim.tick(),&box3dScene,true);
~~~

The identifiers realKinematicBodyHandle, doorDeviceEntity, switchEntity,
sim, regionEntity, physisCellEntity, existingPhysisNereidClimateSampler,
applyExistingDeviceState and dispatchExistingNpcEvent belong to your
existing game host; the example does not invent an engine-wide singleton.

CIRCUIT authored map schemas: CircuitNode/Wire/Gate/Switch/Device. Device
kinds: generic(0), light(1), door(2), motor(3), pump(4), alarm(5).
ECS systems are installed in phase order: switch inputs, power/logic solver,
then device outputs. Box3D kinematic doors are moved only by the real native
physics::Scene; the host callback owns the graphics/FX presentation.

EVOLVE authored map schemas: EvolveRegion and EvolvePressure. A separate
native PHYSIS ClimateLink is optional and must bind to **real** existing
physis::Atmosphere + Surface ECS entities. The EVOLVE region then limits the
real TerraWeave forest ecology nutrient and light climate sampler without
replacing the TerraWeave plant registry.

SOCIETY authored map schemas: SocietyPerson/Contact/Message/NpcTrigger.
NpcTrigger has stable person ID, nonzero knowledge-topic hex mask,
positive actionId, condition mode, once and enabled. The SOCIETY system
records stable transition counters; its native callback routes to the
project's actual NPC/quest systems and should dedupe by trigger Entity
generation plus transition number.

Source code only; these new stages have **not** been compiled or run under
the user's deferred-testing instruction. Refer to handoffs stages 22–25.
