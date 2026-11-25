# AIVERSE World Processes — Stage 18: GRANULAR ↔ TerraWeave, Box3D and Renderer3D

Date: 2026-11-23. Branch: `feature/world-processes-experimental`, draft PR #112.
Main remains untouched. User instruction: **implement functionality now, defer test execution**.

## What was implemented

### 1. GRANULAR tile-to-visual/physics geometry
`examples/world/world_processes_native/GranularTerraWeaveBinding.h`
- Commit `46ddb5a763d8a4ebefbb9e73ca2a0c49a7fa1718` adds `bakeGranularSurface(World&,Entity,EarthworksLayer&,patch window, settings)`. The **actual ECS granular::Field** supplies quantized loose material, while the **actual TerraWeave EarthworksLayer** supplies underlying ground heights. Converts 2D granular field X/row (row increases in the slope-downstream +Z direction) into bounded 3D sand slabs resting on that terrain.
- The same per-cell top heights feed the existing `render::MeshBuilder::box()` geometry and matching Box3D walkable surface triangles (`physics::BodyDesc` with static Mesh). Both projectors consume the same immutable sampled field and soil revision. The Box3D owner remains the only rigid-body collision solver and TerraWeave remains the only earth height owner. This is **not** a second physics engine.
- Separate `GranularBox3DPublisher` and `GranularRenderPublisher` install revisioned per-chunk geometry into their REAL owners. Create replacement before retiring old Box3D body; GPU publisher uses `MeshBuilder::upload`, checks the real RHI allocation and waits for safe buffer lifetime fences before retiring old resources. `draw(Renderer3D&)` queues the real buffers to the existing renderer, not a custom graphics backend.
- Chunk windows are max 32×32 cells with at most 1024 occupied tiles; source field max 128×128. Geometry signature includes entity generation, Terra soil revision, world origin, size, color, occupied mass, ground elevation and patch window to avoid stale reuse.
- Commit `248c8bd9de99eb2de4f41f32697e97194255f1c8` fixes origin-based revision identity and checks Box3D's documented ±100 km coordinate limit.
- Commit `3ae04a993996271a51f924c640a110910ef78b35` rejects unstepped Field snapshots and advances the publisher's last observed tick even for identical geometry (stale higher-level updates cannot be accepted).
- Commit `7571afbf0b82e2bd6fc484be1774ab4caafde271` adds separate safe `retireAbsent` methods for missing physics/RHI tiles, only after successfully publishing the replacement selection.

### 2. Native host fixed-tick/frame barrier and streaming
`examples/world/world_processes_native/GranularWorldIntegration.h`
- Commit `f768d287c52f19c81ce33d5b11ddaaef134220a8`.
- `prepareGranularFrame(World&,EarthworksLayer&,span<GranularResidentTile>, placements, tick)` reads a **completed** ECS tick and the same live Terra soil revision, sorts streaming selections and bakes 16×16 tiles. Rejects stale ECS/soil, duplicate keys and overly large resident budgets before publishing anything. Limit 256 resident tiles and 16384 occupied tiles in a prepared frame.
- `publishGranularPhysicsFrame(Scene&,publisher,frame)`: call on the Box3D owner thread **between** physics ticks, before stepping simulation contacts. Uses the same terrain sampling as GPU, and retires absent chunks only after each requested chunk succeeds.
- `publishGranularRenderFrame(gfx::Device&,publisher,frame)`: call on the GPU owner thread **outside** the Renderer3D frame. Uses the shared frame but a separate lifetime barrier; visual draw occurs through `publisher.draw(renderer)`.
- A failure may leave **some** newly published chunks and some old chunks when a large batch partially succeeds. Host must hold its gameplay/render phase and retry publication rather than stepping on mismatched terrain. The individual tile replacement is transactional, but an entire multi-tile physics/GPU frame is not yet a single atomic cross-device transaction.
- Physics selection must include nearby actors, NOT only tiles visible to the camera. TerraWeave `TerrainBox3DPublisher` remains responsible for base ground, GRANULAR only for loose-material overlay.

### 3. Actual ECS TerraWeave digging/filling with conserved volume
`examples/world/world_processes_native/GranularTerraWeaveTransfer.h`,
commit `d3bf1312dd4155f48479b76591d0b3c3b31155a8`.
- `GranularTerraTransfer` addresses a stable generation-safe granular Field Entity, exact field cell and native `EarthworkBrush` settings. For this explicit transfer policy, **one granular mass unit equals one cubic centimetre** of loose volume; mass is capped by the existing field capacity.
- `transferGranularAndTerrain(World&,TerraWeaveEcsAuthorityConfig&,request,tick,result)` requires existing authoritative `TerraWeaveAuthorityResource` and a completed granular field tick, validates cell and input, and previews the **actual** TerraWeave `EarthworksLayer::sculpt` on a staging copy. Its `EarthworkChange::movedVolumeCm3` is used for exact unit bookkeeping; there is no pi-radius estimate.
- Rejects insufficient cell mass for Fill, insufficient free capacity for Dig, invalid bedrock/brush, invalid Entity/stale generation and mass range before mutation. Pre-detaches the granular COW pool before calling `excavateAuthoritativeEcs`, the canonical **ECS TerraWeave soil and NEREID bank bridge**; after the canonical change succeeds, it credits/debits that exact mass on the same ECS owner barrier.
- The publisher `NativeTerraWeaveWorldBinding` remains responsible for rebuilding renderer, Box3D and NavMesh tiles after the canonical soil revision changes. No RHI/Box3D state writes from the gameplay function.
- **Important:** The host must record/replicate the exact fill/dig command as authoritative input. No client independently converts float Box3D contacts into terrain edits. Concurrent Terra or other soil writes between the staging preview and canonical commit are forbidden. A disagreement in moved-volume preflight raises a logic error and must stop the host for recovery; this bridge assumes the normal one-authority one-tick barrier.

## Integration, file paths and ownership

All code is intentionally placed under `examples/world/world_processes_native/`, above engine owner boundaries. Core `morph/granular` ECS remains in `libraries/include/aiverselib/world/`; physics and render are optional native app dependencies and must not be linked into all regular projects. Host passes exact ECS World and TerraWeave resources, and calls publish functions on their respective owner threads.

These adapters show **real engine usage paths**, not automatic global activation. A native project must opt into TerraWeave ECS ownership + Box3D scene and maintain the publisher lifetimes, just as existing TerraWeave native demos do. Other game genres use the same APIs.

## Tests and status

**Tests deliberately deferred by user request.** The GitHub connector committed the source changes, but no C++ compilation, CTest, sanitizer, Box3D contact test, real Metal/Vulkan/GLES draw, NativeTerraWeaveWorldBinding replay test, pixel capture, or full Mac quick suite has been executed for Stage18. Previous historical CI green predates these edits and does not validate them.

Known limitations:
- GRANULAR uses a quantized 2D projected loose-material grid, not a full granular DEM/SPH solver or native 3D terrain-remeshing algorithm. Display thickness is separate from exact volume-transfer units.
- Box3D collision is a sampled top-surface static mesh per tile, not one rigid body per grain; shape creation can be rejected by Box3D. The publisher returns false and retains the prior body in that case.
- Strongly folded terrain inside a cell is sampled at cell center and may require finer grid resolution for contact precision. Existing TerraWeave remains the authoritative height surface.
- Native host must reinitialize publishers and/or call shutdown after rollback to an earlier tick and force republish from restored ECS/soil; silently reusing future-tick buffers is prohibited.
- MORPH collision shape is still a coarse Box3D proxy; visual vertex deformation doesn't automatically re-tessellate the rigid collision shape.

## Exact next stage

1. Add native example host wiring and a readable usage guide for MORPH+GRANULAR, including real ModelDraw attach, authoritative physics/renderer order, rollback/reset and TerraWeave re-publication.
2. Extend 3D Studio scene gizmos for MORPH springs and GRANULAR field volume and camera clipping/LOD; coordinate with Studio owner rather than duplicating windows.
3. When feature phase is complete, user's chosen consolidated test phase: GCC/Clang/ASan/UBSan, GPU Metal/Vulkan/GLES, Box3D scene, TerraWeave authoritative save+rollback, both scene publications, full project Studio and merged-tree Mac `./run-tests.sh --quick`.
4. Update the handoff with actual CI/test results before any PR merge. PR #112 remains draft.

## Additional source hardening
- Commit `e0469098bcaad34b7b308362c42229886b4a3ad0` in `examples/world/world_processes_native/GranularTerraWeaveBinding.h`: if an external Box3D owner has destroyed a cached scene handle, matching geometry signature no longer short-circuits its reconstruction. The publisher checks `scene.valid(handle)` before reporting the resident surface current. This remains source review only; no native test has been executed.
