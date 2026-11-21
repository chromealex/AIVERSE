# AIVERSE LENS — Stage 48B.3B.2: one-shot unpublished topology+surface-scene lease

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: `feature/lens-motionweave-integration` (main untouched)
Parent: `0c81e2270d7449c4ec1e1b6219354428cadd3015`
Previous: `docs/agent-handoffs/lens-motionweave-integration-stage48b3b1-scene-map-groundfx-staging-2026-11-20.md`

## Completed implementation

- `RtsView::prepareTerrainAtomicFrame(dev,grid,renderer,scene,time,vision)` stages an **unpublished** map topology and camera-visible GroundFX scene into the existing `PreparedSurfaceLease` (not a second GPU owner). Includes full Terrain PreparedMap, forced Roads/Shores, scene-aware GroundFX chunks and Fog Sheet with initial R8 or retained same-size visibility knowledge. All five owners are privately staged behind `OwnerPrepareCommit`.
- No owner's `commit*` runs at prewarm time. The original Terrain surface, Road generation, GroundFX, Fog and last submitted camera frame stay unmodified, except the previous camera epoch is explicitly invalidated to forbid secondary replay.
- The transaction is held across calls with **only stable owner/candidate commit+rollback closures**. All prepare callbacks referencing a stack `MapGrid`, temporary staged surface height function, camera/scene and R8 baseline are cleared before storing the lease. The temporary `RtsView::heightAt` override is scoped to synchronous GroundFX preparation and always reset on exceptions.
- Same Stage 48B.2B input validation covers owner/device, scene/map/vision pointer identities, time, frame stamp, generation, five owner revisions, Grid / scene / camera / visibility fingerprints and tile policy. It runs both AFTER private upload and AGAIN at primary draw; changed source causes reverse rollback and a synchronous `buildTerrainAtomicScene` retry from the actual inputs, not a partial publication.
- `prepareMapFrame` directly recognizes an existing topology lease, returning its validity without repeating old-map prewarm. At the start of `draw()`, before `FramePublicationEpoch::begin`, hierarchy resolution or model height lookup, a valid topology lease publishes once and performs terrain change notifications. The normal primary `publishSurfaceFrame` then prepares only the published-map dynamic visibility/LOD resources. No duplicate full Terrain/Road/Shore/ Fog Sheet upload, and GroundFX visible meshes built in the topology candidate are not invalidated a second time.
- Preserve existing `buildTerrain()`, `buildTerrainAtomic()`, and `buildTerrainAtomicScene()` synchronous APIs. Initial unprimed GPU terrain, planet, hierarchy and 2D tile paths explicitly return false from the new opt-in prewarm rather than advertise unsupported behavior. Renderer must be configured with `beginScene()`.
- No changes to the genre-independent OwnerPrepareCommit or Metal/Vulkan/GLES backend code.
- Hardening: full topology prewarm always forces a private GroundFX map reset rather than using Terrain dimension equality as the proxy for GroundFX revision. The resize regression verifies the new map remains unpublished until draw and then all five owners agree on the new dimensions.

## Changed source

- `modules/rts/view/RtsView.h`: public topology prewarm API, private draw-time lease publication declaration.
- `modules/rts/view/RtsView.cpp`: extends existing one-shot lease and shared source validation, prepares unpublished full map/scene, consumes before draw with stale fallback, reuses normal `prepareMapFrame` protocol.
- `tests/tests.cpp`: `lens_stage48b3b2_unpublished_map_lease_consumes_with_primary`, `lens_stage48b3b2_stale_map_lease_discards_and_rebuilds`, `lens_stage48b3b2_failed_prewarm_preserves_live_map`, `lens_stage48b3b2_resized_map_prewarms_all_five_owners`.
- This handoff.

## Verification genuinely performed

GitHub branch parent and file contents fetched at exact SHA; edits anchored uniquely; new code and tests persisted in a Git tree with an expected-parent HEAD update; source HEAD and changed-file paths will be refetched after publication. No native CMake/CTest/fake-RHI runtime or real Metal/Vulkan/GLES compilation in this environment. **Test source authored, not executed.**

## Explicit limits and risks

- The lease contains topology+camera-visible GroundFX, Roads, Shores and Fog sheet, but NOT the new topology's full Terrain visible LOD/trace height or processed Fog field; those are still stage-prepared against the committed map at the beginning of the next primary `draw()`. The overall `FramePublicationEpoch` is only published after all primary-frame layers finish.
- GroundFX remains budgeted to 16 visible chunks / 128 MiB per prewarm; the rest are lazy. The full map Terrain rebuild remains potentially expensive and duplicates GPU allocations transiently.
- Caller must use a single render-owner thread and keep scene/map/camera/visibility pointers alive; staged callbacks are discarded but conservative fingerprint re-validation dereferences those pointers when primary draw occurs.
- On stale scene/camera/map, the draw fallback intentionally performs the complete synchronous map+scene topology rebuild; this may cause a frame hitch, but prevents presenting mixed generations. If fallback GPU preparation throws, the previous live map stays published, while the camera epoch remains invalid.
- No functional claims about Stage49 full Sprite/MotionWeave/lighting/overlays/2D layers. Consolidated native CMake/CTest and available backend GPU visual validation remain mandatory before merge.

## Next

Stage 48B.3B.3: allow Terrain PreparedMap to prewarm visible LOD/dynamic/trace height against its UNPUBLISHED topology and optionally stage processed viewer Fog without duplicate first-primary uploads; add explicit transient GPU byte budget for topology candidate, and adversarial late mutation tests. Then Stage49 integrate remaining owners before main QA/merge.
