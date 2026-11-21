# AIVERSE LENS — Stage 48A: atomic Terrain / Roads / Shores topology publication

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration; **main untouched**.
Parent HEAD: 42660634bac203b28ca37ce557a6cc95851a35bd
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage47b3c-no-draw-retirement-tier-fallback-2026-11-20.md

## Source changes

- `TerrainRenderer::preparedSurfaceAt(shared_ptr<PreparedMap>, grid, x, z)`: read-only query against the **private**, complete staged terrain CPU surface (including the procedural/detail result), never the old published surface. Validates owner/device/revision, unpublished status, and matching dimensions; rejects a stale/foreign/committed candidate with logic_error.
- `RtsRoadView::prepareRebuild(..., bool forceGround=false)`: optional force-geometry flag for a topology change. Forces private spline meshes, wheel-height surface samples, weather film, preview model to use the new Terrain height even with an unchanged authored RoadNetwork, without modifying the live published road dirty flag or GPU handles during prepare.
- `RtsShoreView::prepareRebuild(..., bool forceGround=false)`: optional force-geometry path for banks, foam, shore props and water network against the prepared terrain height.
- `RtsView::buildTerrain` now constructs a genre-independent `render::OwnerPrepareCommit` cohort on a FULL MAP SIZE/TOPOLOGY transition. Prepare order Terrain -> Road -> Shore. All road/shore CPU/GPU candidates sample the same private `preparedSurfaceAt` surface. A late Shore prepare error rolls back both earlier private candidates; no terrain/road/shore state or retirement queue is changed. Only after `prepareAll` succeeds does `publish()` install the three candidates without new GPU uploads.
- Preserves synchronous editor and client `buildTerrain()`: terrain geometry, road geometry and shore are all up to date before the call returns. GroundFX, Fog Sheet and fields are invalidated AFTER successful topology publication. For same-size incremental terrain sculpting, successful `TerrainRenderer::build` now invalidates ground under both roads and shores for the existing next-primary-draw transaction.

## Files

- modules/rts/view/RtsTerrainRender.h + .cpp
- modules/rts/view/RtsRoadView.h + .cpp
- modules/rts/view/RtsShoreView.h + .cpp
- modules/rts/view/RtsTerrain.cpp
- tests/tests.cpp
- This handoff.

## Tests written (not executed)

1. lens_stage48a_topology_cohort_stages_new_height_and_rolls_back_late_failure: real terrain+road resources, new constant corner elevation, explicit OwnerPrepareCommit with Terrain -> Road -> Shore -> injected failing final owner, verify old Terrain dimensions, road surface generation/height and retirement counts; retry and verify exact staged/new height + no-idle commit.
2. lens_stage48a_rts_view_build_terrain_publishes_new_topology_cohort: exercise actual synchronous RtsView::buildTerrain map dimension transition, confirm new Terrain dimensions, forced Road generation and new height field.

## Verification

- Read GitHub HEAD and all affected files before preparing patch, with exact-source substitutions and source anchor checks.
- Built each new blob in GitHub without moving branch, then assembled them into a single tree/commit and updated branch with expected HEAD check.
- No local Mac source checkout, CMake configuration, native C++ compilation, CTest, Metal/Vulkan/GLES backend compilation, GPU parity or benchmark was run. Test cases are newly written but **unverified**.

## Limitations / risks

1. Scope is intentionally **Terrain + Roads + Shores** only. `GroundFX::prepareFrame` requires a Renderer3D, RtsScene, viewer and vision, which are not available in synchronous `buildTerrain`. Fog height sheet + processed R8 field also require the primary render-frame context and remain staged during `RtsView::draw`. This is not yet a complete cross-owner map+frame transaction.
2. On same-size incremental terrain edits, Terrain publishes during `buildTerrain`; Roads and Shores are invalidated and rebuilt at next frame. A screenshot between calls can show old road/shore geometry against the new terrain. Fix by supplying an externally staged incremental terrain candidate and sharing this map cohort.
3. `paintLayers_.shutdown`, `twoD_->buildTiles` and field/FOG/ground invalidation occur after the three-owner commit and can still fail independently; make their preparation explicit in subsequent stages.
4. Transient full-map topology rebuilding duplicates Terrain, Road and Shore GPU/CPU memory. No fixed cross-owner staging-byte budget or multi-frame incremental staging yet.
5. Old resource release depends on the native completion-token retirement queue; GPU backend fault behavior remains unverified.

## Next step — Stage 48B

Connect the map-source change transaction to the next primary `FramePublicationEpoch` and include GroundFX and Fog using the staged terrain height while preserving editor's synchronous callers. Consider an opt-in scene-aware `buildTerrainFrame` API that stages the Surface, GroundFX, Roads, Shores, Fog Height Sheet/visibility and commits only after every owner has prepared; then integrate editor/clients once tested. Follow with MotionWeave/Sprite, lighting and overlays; continue a dedicated handoff per stage. Run the user's requested consolidated CMake/CTest and supported Metal/Vulkan/GLES compiles after full feature development.
