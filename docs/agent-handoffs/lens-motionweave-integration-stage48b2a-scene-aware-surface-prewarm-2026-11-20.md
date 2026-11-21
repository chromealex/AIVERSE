# AIVERSE LENS Stage 48B.2A — scene-aware surface prewarm, shared primary owner cohort

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (main unchanged)
Parent commit: d033f7c91bd00dee02a6a50b700ae71b22fac6b9
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage48b1-fog-groundfx-topology-cohort-2026-11-20.md

## Implemented / public API

- `RtsView::prepareMapFrame(Renderer3D&, const RtsScene&, double time, const vector<uint8_t>* vision, const MapGrid&)` is an **optional scene-aware warmup** for an already built map. It can be called at render-owner pre-render boundaries to prepare camera-visible GroundFX meshes and the current Roads, Shores, Fog sheet/R8 and Terrain COW/LOD/trace/pipeline resources **without drawing**.
- `RtsView::publishSurfaceFrame(...)` is a private shared helper extracted from `RtsView::draw`. Both `draw` and the optional prewarm execute EXACTLY ONE implementation of the five-owner `render::OwnerPrepareCommit`: GroundFX -> Roads -> Shores -> Fog -> Terrain per-camera packet. All candidates prepare before any owner publishes; a late failure rolls back earlier staged owners.
- The optional prewarm **invalidates** the previous `FramePublicationEpoch` immediately but never marks a new epoch as ready. Only a subsequent successful **primary** `RtsView::draw` can finish that camera epoch. This blocks stale previous-frame secondary RenderTexture/capture reuse after prewarming new GPU handles.
- `prepareMapFrame` returns false without mutation when Terrain is not built, the requested grid dimensions are different from published terrain, the view is in planet mode, or the scene has hierarchy-based entity transforms that need primary draw graph resolution; normal `draw` remains the supported fallback.
- It throws on an attempt during an active primary frame publication or on a GPU resource staging error. It requires the same GPU/render owner thread and should be called between RHI frame submissions. The current 16-visible-GroundFX-chunk/128 MiB per-call budget remains intact and work can be spread over repeated warmup calls.

## Modified paths

- `modules/rts/view/RtsView.h`: one public prewarm API and private shared owner transaction helper.
- `modules/rts/view/RtsView.cpp`: relocate the existing full five-owner transaction to the shared helper, call it from normal `draw`, implement optional map-frame prewarm with explicit frame-epoch invalidation.
- `tests/tests.cpp`: new tests `lens_stage48b2a_scene_prewarm_invalidates_secondary_epoch_until_primary` and `lens_stage48b2a_late_fog_failure_rolls_back_scene_prewarm`.
- This handoff document.

## Validation honestly performed

- Inspected GitHub source at exact parent d033f7c91bd00dee02a6a50b700ae71b22fac6b9.
- Extracted the existing owner transaction verbatim into one shared helper, checked unique source anchors and that the old `draw` invokes the helper instead of retaining a second implementation.
- Created GitHub blobs, tree, commit and updated feature branch after verifying exact expected HEAD.
- **NOT RUN**: local C++ compiler, CMake, CTest, macOS Metal, Vulkan, GLES, actual GPU rendering or benchmarks. Regression tests are written, not executed.

## Scope / known limitations

1. Stage 48B.2A is **not yet** a single atomic *unpublished Terrain topology + scene* transaction. The synchronous `buildTerrain` from Stage 48B.1 already publishes new map owner grids; this API prewarms scene-specific GPU meshes and fog/terrain frame resources afterward. A late prewarm failure preserves the previously published *frame* owners, not the old map before `buildTerrain`. Do not claim full cross-map rollback.
2. Prewarm commits its prepared surface owners, and `draw` then invokes the same cohort again to prepare the new primary frame. Unchanged inputs normally avoid new GPU uploads, but CPU snapshot copying (especially GroundFX) is still duplicated. A future Stage 48B.2B must support a **leased prepared candidate** consumed once by the subsequent primary frame, with source/vision/camera revision validation and explicit discard on mutation.
3. The returned bool indicates successful scene-aware warmup, **not** complete visual-frame readiness or full coverage of all GroundFX dirty chunks under a per-call budget. Camera epochs remain invalid until primary submission.
4. The legacy standalone Terrain and other editor drawing paths retain some on-demand resource uploads; this API does not alter them.
5. Before introducing cross-frame candidate leases, audit renderer/gpu device generation changes, scene mutation, map editor mutations and resource limits to avoid stale/foreign candidates.
6. Sprite/MotionWeave, lighting, 2D tiles/paint layers and overlays still commit outside this five-owner surface cohort.

## Exact next step

Stage 48B.2B: add an opt-in leased preflight token containing staged (but not yet published) map+frame candidates, strict map/scene/camera/vision revision identity, and a no-allocation consume-at-primary-draw contract. Include same-map Detail sculpting in an externally staged owner and evaluate GPU staging budgets. Then Stage 49: generalize transaction participants for Sprite/MotionWeave + lighting + overlays. Run the consolidated native CMake/CTest and Metal/Vulkan/GLES compilation/parity pass after all functions are completed, as requested.
