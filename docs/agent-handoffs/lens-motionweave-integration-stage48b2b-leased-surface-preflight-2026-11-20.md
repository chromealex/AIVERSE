# AIVERSE LENS — Stage 48B.2B: one-shot unpublished surface preflight lease

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (main untouched)
Base HEAD: 2101d2e6b1af77bf1d4ecc24d06cec165fdc4e01
Previous: docs/agent-handoffs/lens-motionweave-integration-stage48b2a-scene-aware-surface-prewarm-2026-11-20.md

## Actual implementation

- `RtsView::prepareMapFrame(renderer,scene,time,vision,grid)` now holds an **UNPUBLISHED**, one-shot `PreparedSurfaceLease` instead of immediately calling the five owner commit functions. The lease retains a live `render::OwnerPrepareCommit` in State::Prepared with individually owned GroundFX, Roads, Shores, Fog Sheet/R8 and Terrain dynamic/LOD/pipeline/trace-height RAII candidates.
- `RtsView::draw` calls the same `publishSurfaceFrame` helper as before, but that helper now **consumes** a matching lease by calling `OwnerPrepareCommit::publish()` (no new GPU creation/upload or new CPU snapshot preparation). Without a matching lease it stages and publishes the entire cohort synchronously, preserving previous primary draw compatibility.
- The five participants' prepare callbacks execute immediately within `stageSurfaceFrame`, then are **cleared** to prevent a dangling MapGrid/scene/reference capture from surviving the prewarm call. Their persistent commit/rollback closures capture only the owner and stable lease fields.
- The private lease's destructor runs the transaction rollback before the participant holders are freed. A late prepare failure or mismatched draw inputs destroys only never-submitted GPU handles, not old published generations. Commit remains one-shot and releases candidate handles through the existing owner-specific retirement system.
- The comparison is conservative: same owner/device, renderer/scene/map/visibility pointer, exact time, frame sequence, terrain generation, tile mode, **Terrain, GroundFX, Fog, Road and Shore state revisions**, plus content fingerprints of relevant map arrays (height/corners/flags/dig/territory/rooms/foundation/field amount/creep), scene viewer/roads/garrisons/lanes, full visibility bytes, and camera view-projection/eye/quality/trace state. Unknown or changed input leads to discard and reprepare; a fingerprint is not a cryptographic hash.
- Road/Shore `stateRevision_` counters are incremented by init/shutdown/map/ground invalidation/commit and (Road) setNetwork/setGhost/setWeather. Terrain/GroundFX/Fog expose existing publication revisions. Map topology builders and terrain detail setters clear outstanding private leases before modifying published owners; RtsView init/reload/shutdown similarly clear them.
- Prewarm continues to **invalidate** the previous `FramePublicationEpoch`, but cannot mark a new camera frame ready. Only full primary draw completing Terrain/Ground/Fog/Sprites/Animation/Lighting/Overlays marks the new epoch ready for secondary RT/capture cameras.
- Public read-only `RtsView::surfacePrewarmStats()` exposes staged, consumed and discarded counts, useful to verify GPU prep reuse and stale-candidate fallback.
- No third-party libraries, no genre-specific code added to `engine/render/OwnerPrepareCommit`, and no changes to Metal/Vulkan/GLES backend code.

## Modified files

- `modules/rts/view/RtsView.h`, `modules/rts/view/RtsView.cpp`: opaque persistent private candidate owner, primary draw reuse, conservative lease validation, cancellation, lifecycle reset, per-prewarm diagnostics.
- `modules/rts/view/RtsTerrain.cpp`: discard lease before map size/detail changes, without exposing incomplete candidate implementation to the translation unit.
- `modules/rts/view/RtsRoadView.h/.cpp`, `modules/rts/view/RtsShoreView.h/.cpp`: source revision invalidation for road/shore mutations and publications.
- `modules/rts/view/RtsGroundFx.h`, `engine/view/VisibilityField.h`, `modules/rts/view/RtsTerrainRender.h`: expose read-only existing publication revisions.
- `tests/tests.cpp`: two fake-RHI regressions for unpublished one-shot reuse and stale visibility/map/camera mutations (plus previous Stage 48B.2A tests now exercise the lease path).
- This handoff.

## Validation really performed

- Inspected live branch files and source API, generated exact GitHub blobs, verified source anchors and the unique five-participant transaction body, planned atomic single-tree publication with expected SHA check.
- **NOT RUN**: CMake, C++ compiler, CTest, Metal/Vulkan/GLES builds, GPU parity, animation playback or performance profiling. New tests and their assertions are authored but unexecuted.

## Known limitations and next step

1. The lease is only for an already published map. It does NOT hold the unpublished Terrain map topology from `RtsView::buildTerrain` across to `draw`; sync editor map topology is still committed before the scene prewarm. Full topology+frame atomicity remains for Stage 48B.3.
2. Scene identity is conservative and is based on owner revisions, pointers and 64-bit content fingerprints rather than full deep ownership snapshots. Caller must keep renderer/scene/map/vision storage alive through consumption, and all calls must execute on the same render-owner thread. No cross-thread or cross-device lease transfers.
3. An invalid lease falls back to the existing synchronous source preparation. A changed input may therefore allocate GPU resources again, which is intentional; a matching lease does not.
4. The existing per-primary GroundFX visible-chunk budget (16 chunks/128 MiB), Terrain fine LOD preparation budget and independent legacy standalone draw paths remain unchanged. No multi-frame deferred full-map staging yet.
5. Sprite/MotionWeave pose state, lighting, world overlays, 2D tiles and paint layers still commit outside this surface owner cohort. No claim of full all-owner frame atomicity.
6. A source owner mutated directly without a revision increment or a map/scene field omitted from the fingerprint is a correctness risk. Future refactoring should centralize fully versioned immutable source snapshots, add a host-enforced mutation token and run adversarial mutation tests.
7. A native compilation and fake-RHI suite must validate the changes before merging into main. Do not treat this handoff's static code checks as test PASS.

**Next**: Stage 48B.3 coordinated unpublished topology+scene transaction and same-map detail editor source changes; then Stage 49 Sprite/MotionWeave/Lighting/Overlays, followed by one consolidated CMake/CTest and supported Metal/Vulkan/GLES build and parity validation.
