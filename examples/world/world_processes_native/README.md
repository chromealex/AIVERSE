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
