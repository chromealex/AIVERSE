# AIVERSE LENS Stage 42 — generic coherent Frame Publication Epoch (2026-11-20)

## Repository, branch and precise commits
- Repository: chromealex/AIVERSE.
- Branch: feature/lens-motionweave-integration (NOT merged into main).
- Starting parent: e1bd08b5e75faea836b4dfbd56010731c9c86932 (Stage 41).
- a86154d684a04bb913651ecf9f71d1e224004b92 — create engine/render/FramePublicationEpoch.h.
- 2567376268e7aaeff9fc542cc00ea1cffa4dec67 — modules/rts/view/RtsView.h: generic epoch owner, published snapshot and readiness contract.
- 0f62c4b70ad33107427e2c88638176a4c86eef83 — modules/rts/view/RtsView.cpp: primary frame epoch RAII, synchronous owner acknowledgments and prepared-camera readiness.
- ec49f2632fb5f2949636c05d923dbab8feb14769 — modules/rts/view/RtsTerrain.cpp: invalidate previous visual epoch before terrain rebuild.
- 38acc01df6598449d21ac59ce4ff788dd61231d7 — apps/client/GameClient.cpp: use committed view epoch as CameraVisualFrameId and gate secondary RT render.
- 60a2ef9a0a3bc58d01196d0a115bc33c59273390 — apps/client/GameClient.h: remove old uncorrelated presentation counter.
- c8921b9e010725c17a362aaf79a59d77c9a0a611 — tests/tests.cpp: three epoch unit tests for owner completeness, abort and stale retry.
- b54105a3f84d54e6e94b01659b3d3263aa2e5a14 — tests/tests.cpp: integrated RtsView primary publication, invalidation and recovery.
- 807b276dbb165c2bd28a2f14a6d9d9c6d576aa59 — RtsView.h: exact epoch ID parameter for prepared terrain/fog and sprite submissions (default 0 maintains call compatibility).
- 1fec4000b2ff5eee02ba654e74066a195e57de52 — RtsView.cpp: reject stale epoch IDs inside prepared terrain/fog/sprite methods.
- 664b73d9242c15a9167ae8426c6d819ca6ea303d — GameClient.cpp: pass the same committed epoch ID into each secondary-camera terrain/sprite submission.
- Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage41-groundfx-batch-camera-packets-2026-11-20.md.

## Actual architecture and semantics
1. engine/render/FramePublicationEpoch.h is an engine-level, genre-independent class. It implements a render-owner-thread synchronous frame publication fence; it owns no RHI handles. Public Owner bit flags: Geometry, Terrain, Ground, Roads, Shores, Fog, Sprites, Animation, Lighting and Overlays.
2. Begin an RAII Ticket with required-owner bitmask, source frame number, map width/height and presentation time. Beginning a candidate immediately invalidates the old published visual frame. A producer marks its bit ONLY after successful preparation. finish() publishes an immutable Snapshot only if every required bit was acknowledged. An exception or scope exit cancels the attempt and prevents partial secondary-camera reuse. Nested tickets, invalid owner bits and stale tickets are rejected.
3. RtsView::draw begins a ticket before its stateful synchronous work, marks Geometry/Terrain/Ground/Roads/Shores in layer callbacks, Sprites/Animation after primary object/sprite processing, Fog/Lighting/Overlays in later providers. After synchronous final flush it calls finish() and throws if any required callback never ran. Planet mode uses its separate Geometry-only owner path and commits only after its provider completes.
4. RtsView::publishedVisualFrame() provides a copy of the generic Snapshot. RtsView::visualFrameReadyFor(grid,time) rejects any active, invalid or mismatched map dimension/time frame. drawPreparedTerrainFog and drawPreparedSprites additionally accept an optional expectedEpoch ID (0 keeps the former call form); stale IDs are silently rejected. The material/detail/map/road update and view initialization/shutdown paths invalidate stale snapshot readiness.
5. GameClient checks visualFrameReadyFor() before starting offscreen EcsCameraRenderer::renderPrepared. CameraVisualFrameId.presentationFrame is now the committed epoch id, not a detached client increment. It passes the same epoch id to prepared terrain/fog and sprite submissions for every secondary RT camera. The primary animation/FX/view update still occurs only once.
6. This does NOT replace RHI retirement token/fence logic. The earlier immutable per-camera GPU packet and Stage 37–41 resource retirement queues continue to own GPU lifetime. The epoch is a logical consumer-readiness fence, not a GPU-in-flight completion signal.
7. There are no new genre dependencies in engine/render. RTS remains only the adapter mapping the generic owner bits to the legacy view's concrete producers.

## Tests authored, NOT run
- frame_publication_epoch_requires_every_owner_before_secondary_camera: partial owner completion must not publish, full completion yields ready token; wrong map dimensions or time rejected.
- frame_publication_epoch_aborts_late_owner_failure_and_never_replays_previous: previously published epoch invalidated by next attempt; artificial late Fog failure destroys Ticket and leaves no playable epoch; recovered frame receives a strictly newer ID.
- frame_publication_epoch_rejects_incomplete_and_nested_publications: nested ticket rejected, forbidden owner mark rejected, incomplete finish invalidates and moved tickets publish normally.
- rts_view_commits_visual_epoch_only_after_primary_and_invalidates_on_map_edit: real RtsView and Renderer3D fake RHI; primary draw publishes, invalid material change cancels previous epoch, subsequent primary draw publishes newer frame and shutdown invalidates it.
- The existing Stages 37–41 tests remain in tests/tests.cpp.

## Validation actually performed
- Inspected current RtsView.cpp/.h, GameClient.cpp/.h, RtsTerrain.cpp, CameraModelSnapshot.h, Renderer3D.h and last handoff.
- Updated source and tests via connected GitHub and re-read affected files to check the owner marks, RAII cancellation, exact-token secondary playback and client gating.
- NOT RUN: CMake configuration, native C++ compilation, aiverse_tests/CTest, Metal/Vulkan/GLES backend builds, actual multi-camera GPU runs, pixel parity, profiling. The newly written test cases are assumptions to validate in the user's requested consolidated run, not PASS evidence.

## Crucial limitation — this is NOT a full all-owner GPU rollback
Existing Terrain, GroundFX, Fog, Roads, Shores, Sprite and Animation producers publish their state during RtsView::draw. If a late producer fails after an earlier producer published, FramePublicationEpoch prevents any *new secondary camera* from consuming this incomplete frame, but DOES NOT undo those earlier GPU/state mutations. Therefore do not claim an atomic multi-owner GPU transaction or a full immutable snapshot of every owner. The primary frame may also have queued partial Renderer3D draw callbacks; host must handle failed submission safely.

## Remaining risks / next agent
1. Stage 43: introduce real prepare->commit/rollback per-owner API with unpublished resource bundles and a single common publish boundary, preserving all previous owner's live GPU buffers/textures on a late failure. Start with Fog, Roads, Shores and sprite resource kits, then Terrain and GroundFX; coordinate visual clocks and MotionWeave packets. Abort must reclaim only never-submitted resources.
2. The initial active RtsView draw call may still mutate CPU animation/gameplay presentation state even if a late GPU owner faults. Next design should stage immutable CPU presentation clocks and avoid publishing until all mandatory owners have finished.
3. On a failed primary frame, the client currently exits via exception before secondary rendering; its old CameraModelSnapshot may still hold borrowed model references. Existing CaptureGuard and resource owner recovery apply; never submit those packets after cancellation.
4. A single frame can render secondary cameras only after all required owners marked; legitimate missing optional providers should be tracked explicitly rather than marking fictional work. Current implementation marks even when an optional subsystem is disabled, which means 'no work required' for that owner, not successful GPU work.
5. The direct legacy drawPreparedSprites(renderer) and drawPreparedTerrainFog(renderer,grid,vision,time) calls remain available and use latest committed epoch but no *caller-provided* ID; prefer passing the ID for deterministic multi-RT capture.
6. MotionWeave branch feature/universal-ecs-animation-pipeline is not yet conflict-aware merged. Preserve editor/animation RtsView modifications.
7. Maintain the user's requested one-handoff-per-stage rule. After remaining features, perform one consolidated native CMake/CTest and supported Metal/Vulkan/GLES compilation, fix regressions, then consider merging into main.
