# AIVERSE World Processes — Stage 19: physical native host lifecycle

Date: 2026-11-23. Branch: feature/world-processes-experimental.
Draft PR #112: https://github.com/chromealex/AIVERSE/pull/112
No main merge. User specifically asked to finish functionality before running tests.

## Scope and implementation

**Ready-to-use optional facade** at
examples/world/world_processes_native/PhysicalSceneHost.h,
commit bc48a19284f254faafafe9493be552685da31d42.

This class joins Stage17 and Stage18 without installing Box3D into ordinary
RTS/FPS games or adding an engine-to-library dependency:

- addMorphVisual(instanceId, device, actualModel, pristineVertices, sourceIndices,
  vertexInfluences): allocates one model-instance-specific dedicated dynamic
  GPU vertex stream for MORPH. Source model remains owned by the game.
- updateMorphVisuals(const World&), attachMorphVisual(instanceId,ModelDraw&):
  evaluates actual ECS Node offsets on the rendering owner thread and attaches
  the existing Renderer3D ModelVertex override to that object's draw packet.
- collectMorphPhysicsInputs(Box3D Scene, World, bodyHandle->Entity map, nextTick)
  reads actual physics begin contacts and quantizes impulses; the authoritative
  host records/replicates them as integer inputs.
- applyRecordedMorphInputs(World, inputs, nextTick): queues persistent one-shot
  MORPH impacts at the correct fixed tick; never directly modifies the physics
  scene or regenerates float contacts on a network client.
- prepareGranular(World, TerraWeave EarthworksLayer, residentTiles, placements,
  completedTick): builds one immutable, bounded projected loose-sand patch
  frame from matching canonical ECS+soil revisions.
- publishPhysics(Scene,frame) and publishRender(Device,frame) publish that
  SAME source frame on their respective owner threads.
- drawGranular(Renderer3D) submits the existing renderer's real uploaded meshes.
- transferTerrainMass(World,TerraWeave authority cfg,request,tick,result)
  credits/debits GRANULAR quantized mass while invoking the canonical TerraWeave
  ECS soil Dig/Fill operation.
- resetAfterRollback(Scene,Device) invalidates retired GRANULAR RHI and Box3D
  projectors after an ECS rollback. MORPH streams reconstruct model vertices
  from the restored World on the next presentation frame.
- shutdownAfterIdle(Scene,Device) explicitly retires GPU streams, then
  granular graphics and physics resources through their lifecycle barriers.

**README and native example:** examples/world/world_processes_native/README.md,
commit c978d6c60c6ed196f83f8c9255a879a5c927d5f9, includes a C++20
integration lifecycle showing correct Box3D step/contact -> replay inputs,
completed ECS tick -> terrain sample -> RHI and physics publish, ModelDraw
override, canonical TerraWeave fill/dig and rollback restore.
It is an example for a concrete game's existing Simulation/physics/render owner,
not an engine-wide service that automatically creates those owners.

## Architectural boundaries

- Core gameplay World is still the *only* MORPH/GRANULAR authoritative simulation.
- TerraWeave ECS resource is the *only* canonical height/soil authority, NEREID
  remains owner of water/sediment; the host does not fork their state.
- Box3D Scene is the sole rigid-body solver; MORPH vertex positions are visual
  and the coarse proxy collision hull does not automatically deform.
- Renderer3D is the sole graphics backend; the adapters use its standard
  ModelDraw vertex stream and MeshBuilder mesh/draw methods.
- Runtime GPU and Box3D buffers have separate owner-thread lifetime fences.
  A batch of tiles is safe per tile but not guaranteed cross-device all-or-none;
  callers must gate frames/simulation and retry when any publish returns false.
- Source VertexInfluences are authored/loaded by the *game*; this stage does
  not invent a generic automatic rigging algorithm for every arbitrary glTF.
- Network peers consume server-authoritative quantized impact commands.
  Raw float Box3D contacts cannot be independently re-simulated as deterministic
  cross-platform MORPH inputs.
- This code is above the engine in examples/world; the minimal
  examples/native/world_processes module still has no Box3D dependency.

## Code ledger

Stage17:
- 89bed7b1c45298f356ac8a28cf92f541c6fca73f — MORPH persistent impact.
- 882fdab403933847f53da9f83a40b7e2ee310c58 — real Renderer3D vertex stream.
- 359431c1626fa0ae6115ab8ad5cb03a9a4ca02d6 — remove unused scratch.
- 548a660767f94cd87f3fd1e73ecafced6fc90a73 — Box3D contact bridge.
- 0cb7fea36186c5c747bc6c18fd441631699fe320 — Stage17 handoff.

Stage18:
- 46ddb5a763d8a4ebefbb9e73ca2a0c49a7fa1718 — granular terrain/physics/RHI.
- 248c8bd9de99eb2de4f41f32697e97194255f1c8 — revision/coordinate gates.
- 3ae04a993996271a51f924c640a110910ef78b35 — sampledTick/stale revision fix.
- 7571afbf0b82e2bd6fc484be1774ab4caafde271 — per-publisher tile retirement.
- f768d287c52f19c81ce33d5b11ddaaef134220a8 — completed tick frame orchestration.
- d3bf1312dd4155f48479b76591d0b3c3b31155a8 — canonical TerraWeave soil transfer.
- 3c07ecdcbc9d6ee2179e28ece6b5d853bc233150 — Stage18 handoff.

Stage19:
- bc48a19284f254faafafe9493be552685da31d42 — native PhysicalSceneHost facade.
- c978d6c60c6ed196f83f8c9255a879a5c927d5f9 — native integration README.
- This handoff commit: git log -1 --
  docs/handoffs/world-processes-stage19-physical-native-host-2026-11-23.md.

## Tests, evidence, and next step

**Tests deliberately deferred.** No CMake/GCC/Clang/ASan, macOS Metal,
Vulkan/GLES RHI, real Box3D/Renderer3D native frame, pixel screenshot or
full merged-tree Mac quick test was executed for Stages17–19. The GitHub
connector accepted source commits only. Historic Stage05 green C++ tests
do NOT validate these new optional adapters or changed MORPH schema.

Next implementation (if features remain priority): add native Editor/Studio
visual gizmos for MORPH node/spring influence and GRANULAR field bounds;
build a real authoring fixture with a MORPH-deformed glTF and a TerraWeave
ground + sand grid, ensure Scene selection, scale and origin handling.
Coordinate with the Studio branch owner and preserve its design system.
Then move through CROWD/NavMesh, IMPRINT traces, CIRCUIT device bindings,
EVOLVE environment and SOCIETY NPC AI.

Before PR #112 leaves DRAFT: reconcile current main and feature/studio-html5,
run actual Mac build and complete native CTest (including Box3D/TerraWeave),
GPU Metal/Vulkan/GLES visuals, save/rollback/peer parity, full
./run-tests.sh --quick and document real pass counts and screenshots.
