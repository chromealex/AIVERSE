# AIVERSE LENS Stage 48B.1 — Fog + GroundFX join synchronous map-topology cohort

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (**main not changed**)
Parent HEAD: a77cb81c32fb89c9563dcb92b54782fcea7fef85
Prior handoff: docs/agent-handoffs/lens-motionweave-integration-stage48a-atomic-terrain-road-shore-topology-2026-11-20.md

## Purpose and boundaries

RtsView::buildTerrain is called synchronously by client and editor without a Renderer3D, scene, viewer mask, or animation time. Stage 48A published Terrain/Roads/Shores together but left the old map-sized GroundFX and Fog GPU state live until the next primary draw. Stage 48B.1 adds their **map-boundary owner state** to the existing five-owner prepare/commit protocol while leaving scene-specific GroundFX mesh creation in the primary frame.

This is NOT a full all-visual-owner transaction. Sprite/MotionWeave, material paint and 2D tiles remain separate, and GroundFX visible chunk meshing still runs during the primary draw.

## Actual source modifications

1. modules/rts/view/RtsGroundFx.h/.cpp:
   - New `RtsGroundFx::prepareMapReset(const MapGrid&)` creates an opaque PreparedFrame **without Renderer3D/RtsScene**. It allocates only private CPU grid/visibility arrays and clean chunk slots, retains immutable kit ownership in the live view, stages no GPU meshes, and marks old lane ribbon invalid.
   - Its normal `commitFrame` now handles this candidate via the existing fullReset + changedLane transaction: GPU-live old chunk/lane VBs and IBs retire using NativePresentationRetirement. Discarding a candidate does not touch the old owner. Missing/unchanged map returns an empty candidate.
   - Read-only `mapMatches(w,h)` detects a published CPU grid matching target dimensions. Visible mesh fills are deferred to `prepareFrame` on the next primary camera, honoring the existing 16 chunk / 128MiB staging budget.
2. modules/rts/view/RtsTerrain.cpp:
   - Full topology `OwnerPrepareCommit` order becomes **Terrain -> Roads -> Shores -> GroundFX grid -> Fog Sheet + Fog R8**. Every dependent surface samples the fully prepared but unpublished Terrain height. A late Fog GPU upload failure rolls back every earlier staged owner.
   - Fog staging uses `VisibilityField::prepareSheet` and `prepareUpdate` from Stage 46 with a private all-**unexplored** 0 mask for the new map. It does **not** expose previously unknown terrain, and it does not claim to know a viewer's scene/vision at the synchronous map boundary. The next primary draw stages the real mask and fade.
   - On successful topology commit, `fogMeshDirty_` clears because the new height-sheet already matches the committed terrain. On same-size sculpt edits, `fogMeshDirty_` remains true until the next primary owner transaction, matching prior behavior.
   - `framePublication_.invalidate()` still executes at the beginning of buildTerrain: old secondary RT/capture epochs remain unavailable until the next full primary draw publishes all required owners.
3. modules/rts/view/RtsView.h:
   - New read-only `surfacePresentationMapReady(grid)` reports when Terrain dimensions, GroundFX grid dimensions, and Fog sheet dimensions agree. It is NOT a signal that sprites/animation or GPU-visible GroundFX chunks are ready.
4. tests/tests.cpp:
   - `lens_stage48b1_fog_gpu_failure_rolls_back_entire_topology_cohort`: inject R8 allocation failure **after Terrain, Roads, Shores and GroundFX candidates have all prepared**, assert old Terrain/GroundFX/Fog dimensions, road generation, live Fog texture and all retirement queue lengths are unchanged, retry with full idle denied, verify coherent publication.
   - `lens_stage48b1_editor_map_build_prepares_fog_and_groundfx_before_camera`: call synchronous RtsView::buildTerrain from old to new size without a scene/camera, assert `surfacePresentationMapReady`, old frame epoch invalidation, then render one primary camera and check frame epoch publication.

## Verification actually performed

- Inspected current GitHub HEAD and source/API of TerrainRenderer, RtsView, GroundFX, VisibilityField, the generic OwnerPrepareCommit and existing fake-RHI test fixtures.
- Created source blobs, will assemble one atomic tree/commit guarded by expected exact parent HEAD and refetch new paths.
- **NOT RUN:** any native CMake configure/compile, CTest, macOS Metal, Vulkan, GLES, GPU parity, timing or memory benchmark. Regression assertions are written but UNEXECUTED.

## Limitations and risk register

- GroundFX's old scene meshes are retired at topology commit, while new map's visible chunk meshes are generated in the next primary frame. The generic FramePublicationEpoch prohibits secondary camera reuse of old frame, but do not claim complete visible frame publication until that primary draw finishes.
- Same-dimension terrain detail/sculpt updates still commit Terrain first and flag Road/Shore/GroundFX/Fog for the next draw; a fully atomic same-size editor transaction needs a separate incremental prepared-candidate API.
- Paint layers, 2D tile data and field overlays are still invalidated/rebuilt AFTER this five-owner topology commit; failures here can leave other owners committed. Include these in a later preflight.
- The private all-unexplored Fog R8 map is a transition baseline; gameplay visibility policy still comes from the actual host-supplied vision mask on primary draw. It is not an authorization mechanism.
- Full-size map transition temporarily duplicates Terrain/Road/Shore/Fog GPU resources; GPU staging byte cap and device-loss recovery tests remain pending.
- Calls across different GPU owner threads or a stale staged Terrain source must continue to fail closed. Actual RHI retirement/GPU idle behavior has not been proven.

## Precise next stage — Stage 48B.2

Add a **scene-aware optional prepareMapFrame** variant that can stage Terrain topology, complete visible GroundFX meshes, Roads, Shores, Fog visibility and Terrain camera LOD/trace resources together against the new terrain height, and commit only at one primary frame boundary. Preserve the current synchronous `buildTerrain` for editor clients and do not introduce a render-only dependency into the generic view or simulation. Then finish same-size Terrain incremental staging, 2D paint/tiles, sprites/MotionWeave, lighting and overlays. Follow the user's per-stage handoff requirement. Perform one consolidated CMake/CTest + supported Metal/Vulkan/GLES compilation and runtime parity validation after functional completion.
