# AIVERSE LENS — Stage 48B.3B.1: unpublished scene-aware GroundFX map preparation

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: `feature/lens-motionweave-integration` (**main unchanged**)
Parent: `48b2bad766e80c350966a87b8ed0c91a2c852e44`
Previous: `docs/agent-handoffs/lens-motionweave-integration-stage48b3b0-topology-source-version-validation-2026-11-20.md`

## Implemented

- `RtsView::buildTerrainAtomicScene(dev,grid,renderer,scene,time,vision)` explicitly opts into staging the **camera-visible GroundFX GPU meshes** at the same time as unpublished full-map Terrain, Road, Shore and Fog resources. The existing synchronous `buildTerrain()` and `buildTerrainAtomic()` signatures and semantics remain unchanged.
- `RtsGroundFx::prepareMapSceneFrame` fills the private `prepareMapReset` clone **in place** by reusing the existing bounded frame mesh builder (`16 chunks / 128 MiB`); `prepareFrame` now shares that builder through `prepareSceneIntoCandidate`. It avoids two private mesh clones and preserves the full-reset old-lane retirement marker.
- A strictly scoped, non-owning height override in `RtsView::heightAt`, valid only for the exact `MapGrid` address during GroundFX staging, samples `TerrainRenderer::preparedSurfaceAt` instead of old published geometry. It is cleared on success AND exceptions; no changes to generic OwnerPrepareCommit or RHI backend APIs.
- `buildTerrainImpl` skips the post-transaction `groundFx_.invalidate()` for scene-prepared geometry. Remaining off-camera dirty chunks use the existing future-frame lazy budget. Map input fingerprints and five-owner revisions from Stage 48B.3B.0 are still checked after ALL preparation and before publication.
- Post-implementation hardening: scene, camera and visibility fingerprints from the existing LENS prewarm path are now also validated for the new scene-aware map commit. A fake-RHI callback that mutates `RtsScene::viewer` during a GroundFX mesh upload is covered by the authored regression, which must reject the private topology cohort without changing live Terrain, Roads or retirements.
- Primary frame publication remains draw-only. This is **a synchronous map+partial-scene cohort**, not a persistent unpublished cross-call lease; only visible GroundFX is included so far.

## Source files

- `modules/rts/view/RtsGroundFx.h/.cpp`: explicit map-scene candidate API and shared builder.
- `modules/rts/view/RtsView.h/.cpp`: explicit scene map entry point, transient height staging fields and shared scene/camera/visibility fingerprint wrappers.
- `modules/rts/view/RtsTerrain.cpp`: local height override, six-input overload and GroundFX participant integration.
- `tests/tests.cpp`: `lens_stage48b3b1_scene_groundfx_uses_unpublished_terrain_and_rolls_back`.

## Regression test authored (NOT RUN)

The fake GPU device scans staged mesh vertex buffers for vertices around the new height of 2.0 while the published Terrain is still at 0.0. It first deliberately rejects a matching scene mesh upload and checks the live height, road generation, resource retirement counts and published epoch stay intact. It next injects an in-place scene viewer mutation during GPU preparation and checks a successful private upload is still rejected because its scene fingerprint changed. It finally retries with a stable scene and GPU idle disabled, checks that the new heights/road generation publish as a cohort, and draws the new primary frame.

No native compiler, CMake, CTest, GPU capture or Metal/Vulkan/GLES parity executed in this workspace. Test results must **not** be represented as passing until run on the user's source checkout.

## Limitations and next

1. The caller MUST have configured `renderer.beginScene(camera,...)` before calling the new scene-aware map API; preparation is synchronous on the render-owner thread, with unchanged current 16-chunk/128-MiB GroundFX budget.
2. This does not yet stage processed Fog visibility, LOD and terrain trace-height for the unpublished topology. Those remain in next primary-frame preflight.
3. A map with never-initialized Terrain GPU kit follows the legacy initial build and does not claim scene preparation.
4. Stage 48B.3B.2: own a one-shot unpublished Terrain map candidate across calls and join its source/scene/owner validation with the existing LENS prewarm lease. Avoid ever storing transient height-function pointers in the persistent candidate.
5. Stage 49: integrate Sprite/MotionWeave, lighting, overlays and 2D/paint layers, then run one consolidated native, fake-RHI, Metal/Vulkan/GLES build + visual/capture QA before main integration.

Strict handoff rule: record actual test evidence, constraints, and next work in a new document at every follow-up stage.
