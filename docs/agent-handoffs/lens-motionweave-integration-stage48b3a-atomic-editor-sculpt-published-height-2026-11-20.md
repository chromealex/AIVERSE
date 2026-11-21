# AIVERSE LENS — Stage 48B.3A: opt-in atomic same-size editor sculpt + published ground heights

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (**main unchanged**)
Parent commit: 3cebef1191ea6c51cea6840d62696f66820fef97
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage48b2b-leased-surface-preflight-2026-11-20.md

## Goals / achieved scope

The synchronous editor can edit a MapGrid in-place, and the old terrain must remain visible until the new GPU/CPU surface **and its dependent Roads, Shores, GroundFX and Fog sheet** all succeed. Existing buildTerrain uses sparse chunk and detail paths for speed; Stage 48B.3A introduces an **explicit opt-in full map transaction** for edits requiring stricter atomicity.

**Important:** This is Stage 48B.3A, not yet a single unpublished map topology + scene-aware animation/meshes transaction. Full scene+topology lease is the next stage.

## API / architectural changes

1. `RtsView::buildTerrainAtomic(gfx::Device&, const MapGrid&)` (RtsView.h / RtsTerrain.cpp)
   - Preserves current `buildTerrain()` signature and its fast incremental sculpting behavior. `buildTerrainAtomic()` delegates to common `buildTerrainImpl(...,forceAtomic=true)`.
   - For already built maps even when width/height are unchanged, stages the full candidate Terrain with `TerrainRenderer::prepareMap()`, then builds Road and Shore from `TerrainRenderer::preparedSurfaceAt`, prepares GroundFX grid reset, and prepares the Fog sheet. Only once all five owners succeed does the generic OwnerPrepareCommit publish, with no full-GPU-idle requirement.
   - Full-size map transitions continue to use the existing new-map unexplored Fog R8 field. Same-size edits with a matching published Fog sheet **do not reset visibility**: they stage the new height sheet only, retaining the processed R8 and fade/target/cur viewer memory. If there is no matching Fog sheet yet (e.g. editor calls before first primary draw), they fall back to the correctly sized initial R8 baseline.
   - `buildTerrainAtomic` first cancels any previously staged prewarm GPU lease. Successful commit invalidates the prior secondary camera epoch, updates dependent surface owners and leaves full primary-frame publication for the next draw.

2. `VisibilityField::commitPreparedSheet(unique_ptr<PreparedSheet>) noexcept` (engine/view/VisibilityField.h/.cpp)
   - Read-only matching size/owner/device/revision validation before commit, fail-closed if the candidate was for a map resize. Retires prior sheet VB/IB via NativePresentationRetirement; transfers new sheet handles; **does not change** R8 texture, blurred/target/current fog field, fade clock, or explorer knowledge. Increments the owner revision.

3. `RtsGroundFx::prepareMapReset(const MapGrid&,bool forceSameSize=false)` (RtsGroundFx.h/.cpp)
   - The full source ownership reset can now be forced even on equal dimensions. A same-size forced reset retains the player's last observed territory/dig/room/mark and blend memory, resets only mesh geometry and marks it dirty for scene-aware first-primary preparation. A cancelled candidate leaves old GPU buffers and memory untouched.

4. **Published immutable CPU terrain height source** (RtsTerrainRender.h/.cpp, RtsTerrainDetailMesh.cpp, RtsTerrain.cpp)
   - The editor modifies `MapGrid` **before** `buildTerrainAtomic()`; querying its mutable corners directly previously exposed new heights against old published GPU geometry, including after failed uploads. Now TerrainRenderer owns a minimal snapshot of **only** flags, level, fine heights and heightfield corners (no gameplay/navigation/network arrays). A snapshot is copied **before** any terrain build attempt, installed only on successful build, and swapped with the full staged Terrain owner in `commitMap()` without allocations.
   - `TerrainRenderer::surfaceAt` and `RtsView::heightAt` now use the actually published surface (or published detail), independent of in-place editor source changes. Before the first build they still fall back to the supplied source.
   - Trade-off: snapshot copy is O(map cells) per terrain build, even on a potentially unchanged build. Optimize through cached source revision, editor change sets or COW snapshots once native correctness is proven.

## Modified code

- engine/view/VisibilityField.h, engine/view/VisibilityField.cpp
- modules/rts/view/RtsGroundFx.h, modules/rts/view/RtsGroundFx.cpp
- modules/rts/view/RtsView.h, modules/rts/view/RtsTerrain.cpp
- modules/rts/view/RtsTerrainRender.h, modules/rts/view/RtsTerrainRender.cpp, modules/rts/view/RtsTerrainDetailMesh.cpp
- tests/tests.cpp
- This handoff

## Tests added — **not executed**

- `lens_stage48b3a_same_size_fog_sheet_keeps_explored_r8_history`: injected Fog index-buffer failure rolls back with old R8/field unchanged; successful no-idle sheet-only commit changes VB/IB retirement count but preserves R8/field.
- `lens_stage48b3a_opt_in_atomic_sculpt_keeps_sync_editor_contract`: prime live scene and fog, mutate corners of the **same MapGrid object**, inject a private terrain GPU failure, check old published height and Road generation; retry without full GPU idle, check coherent new heights and road generation, then draw a new primary frame.
- `lens_stage48b3a_atomic_same_size_map_before_first_camera_uses_r8_baseline`: same-size atomic sculpt before first camera/Fog setup must fall back to a valid initial R8 texture instead of trying the illegal sheet-only commit.

## Verification genuinely performed

- Examined current GitHub feature branch HEAD and actual RtsTerrain/Detail/VisibilityField/GroundFX APIs.
- Added source changes using exact anchor edits, checked staged source blobs, and published one commit guarded by expected HEAD SHA. Confirmed committed paths via GitHub.
- **NOT RUN**: CMake, compiler, CTest, Metal, Vulkan, GLES compilation, frame capture, GPU runtime or benchmarks. These tests are **unverified**, not PASS.

## Limitations and next step

- **Opt-in:** the existing fast `buildTerrain()` still may publish same-size incremental chunks individually. It does not promise all-owner rollback on a later GPU fault. Clients demanding same-size atomicity must call the new `buildTerrainAtomic`.
- The `buildTerrainAtomic` full-map path duplicates all terrain meshes, road models and Fog Sheet and can be expensive on giant worlds. No transient GPU byte budget or incremental copy-on-write geometry journal across *all* owners yet.
- New GroundFX scene meshes and processed viewer fog still build on the next primary `draw()` (or optional prewarm lease); the synchronous API cannot know camera/scene/vision.
- There is no single map+scene candidate lease across synchronous map changes and subsequent primary frame yet. Stage 48B.3B should provide a **scene-aware transaction over unpublished topology** with a scoped temporary published-height override for GroundFX, then do final source-revision and device-generation validation.
- Asset paint layers/2D tile maps and Sprite/MotionWeave/Lighting/Overlays remain outside the five-owner surface transaction. Correctness tests and GPU parity are pending.
- Because the user asked to complete functionality before one full build/test pass, keep further stages isolated with handoffs and run consolidated native CMake/CTest plus Metal/Vulkan/GLES backend checks after functionality is implemented.

## Next

Stage 48B.3B — scene-aware unpublished Terrain topology+GroundFX visible mesh+Fog/LOD transaction with cross-owner height policy, bounded GPU work and discard/commit on source mutation. Then Stage 49 — sprites/MotionWeave, lighting, overlays. Each stage requires a handoff with modified files, actual tests, limits, branch and next step.
