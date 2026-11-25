# AIVERSE World Processes — Stage 17: MORPH real render and Box3D contact adapters

Date: 2026-11-23
Repo: chromealex/AIVERSE, feature branch `feature/world-processes-experimental`, draft PR #112.
User requested features before tests. Main and the canonical engine render/physics owners are unchanged.

## Implemented

**1. MORPH one-shot authoritative collision inputs**
`libraries/include/aiverselib/world/MorphEcs.h` commit `89bed7b1c45298f356ac8a28cf92f541c6fca73f`.
- New persistent `morph::Impact{x,y,z,applyTick,pending}`, storage schema `morph.impact` version 1.
- `morph::enqueueImpact(World&,Entity,nodeTick,dx,dy,dz)` merges multiple bounded quantized collision impulses targeting the same node and exact future tick. Fails on stale/invalid node or conflicting pending tick.
- `morph::advance(World&,tick)` stages existing springs/Force and due Impact, consumes each pending impact exactly once, commits Node and Impact together after validation. Pending inputs survive COW snapshots, v10 save/load and replay as canonical ECS state.
- Impact and continuous Force are deliberately different; continuous Force is not abused for contact callbacks.

**2. Real ModelVertex/PBR stream adapter**
`examples/world/world_processes_native/MorphModelStream.h` commit `882fdab403933847f53da9f83a40b7e2ee310c58`; cleanup `359431c1626fa0ae6115ab8ad5cb03a9a4ca02d6`.
- `MorphVertexBinding` stores per-source-model vertex index and up to four generation-safe ECS node references with normalized unsigned-16 weights (sum 65535).
- `MorphModelStream::initialize(device,model,pristine ModelVertex span,indices span,bindings,config)` requires the **existing** renderer's dedicated GPU geometry, valid primitive layout, bounded CPU source, and stable source vertex IDs. The host must set `model.batching.allowGeometryPool=false` before the model is uploaded.
- Per presentation frame `stream.update(const World&)` reads real MORPH Node offsets vs rest values, applies weighted local 3D displacement to actual `render::ModelVertex` positions, optionally recomputes triangle normals and orthogonalizes tangents, then writes one full dynamic 60B vertex stream with `gfx::Usage::Dynamic` and `snapshotUpdates=true`. No shared geometry pool writes, no renderer reimplementation, no ECS changes from presentation.
- `stream.attach(ModelDraw&)` sets the real `vertexOverride`, `vertexOverrideCount` and enlarged culling `maxVertexOffsetMetres`; standard ModelDraw shading, shadows, pose, LOD, outlines, PRISMA, etc. remain owned by Renderer3D.
- `shutdownAfterIdle()` waits for the GPU resource barrier before releasing its own vertex buffer. The host must retire old ModelDraw packets before shutdown and must rebind on model reload or ECS replay restore.
- Binding is per **visual instance**, not per global shared glTF asset.

**3. Real Box3D contact adapter**
`examples/world/world_processes_native/MorphBox3DContactBridge.h` commit `548a660767f94cd87f3fd1e73ecafced6fc90a73`.
- `collectMorphContactInputs(Scene&,World&,BodyHandle->Entity,nextTick,...)` reads actual Box3D `Scene::contactBegins` and body velocities, converts contact closing speed and normal to signed, bounded integer impulses, sorts by full Entity identity and body handle, and never writes into Box3D or the ECS during collection.
- `enqueueMorphContactInputs(World&,span<MorphContactInput>,tick)` preflights targets and pending ticks before queuing quantized inputs on the ECS owner thread. Only the **authoritative server/host** may collect from float Box3D: network clients/replay must consume the **recorded/replicated integer inputs** to avoid cross-platform floating-point divergence.
- `makeMorphProxyBox` creates a host-owned **Box3D kinematic proxy descriptor**; the physics Scene remains the only rigid collision solver.

## Actual validation
**Tests were intentionally not executed**, at the user's explicit request. No GCC/Clang/ASan/CTest, GPU native frame, Vulkan/GLES renderer, macOS screenshot, or real Box3D contact gameplay check has been performed for Stage17. Source was committed to the GitHub feature branch. Any claim of production readiness requires future validation.

## Limits, ownership, next step
- This implements real vertex deformation through existing dynamic RHI and real Box3D-to-MORPH impacts, but does **not** deform Box3D rigid shapes to match all soft-body vertices. Collision proxy remains coarse; no FEM or collision-aware self-contact solver.
- The native host must instantiate the stream, supply exact vertex correspondence and convert contact buffer to a scheduled authoritative tick. Merely including the header does not create a GPU resource or attach to existing gameplay actors.
- Core modules do not include Box3D or renderer; opt-in examples are above the engine layer.
- Next: Stage18 GRANULAR mesh/terrain physicalization using TerraWeave ground collision and Box3D, with revisioned host publication and read-only RHI data, then handoff.
- After the requested feature phase: integrate real game project with examples and run consolidated GPU/Box3D/CMake/replay/rollback QA before any main merge.
