# AIVERSE LENS — Stage 46: Fog height sheet joins the owner transaction

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (main untouched)
Parent: 84fa59052c0f0a6bec8fd86bcf99c45cb3846448
Previous: docs/agent-handoffs/lens-motionweave-integration-stage45-road-shore-fog-cohort-2026-11-20.md

## Implemented (source changes, not native-validated)

- engine/view/VisibilityField.h/.cpp: introduced RAII PreparedSheet. It owns staged vertex/index GPU buffers, the target map dimensions and a new blank field baseline for map resizes. Its destructor frees unpublished buffers. prepareSheet() generates sheet geometry without mutating published state and without waitForResourceIdle().
- prepareUpdate(..., const PreparedSheet*): accepts the staged dimensions and (for resize) the staged blank CPU baseline. Creates and uploads a private R8 texture for the correct map size. A GPU upload error leaves the live sheet and live field unchanged.
- commitPreparedFrame(sheet, field) noexcept: checks the single-thread source revision, retires the old sheet VB/IB through NativePresentationRetirement (GPU completion tokens), publishes new sheet geometry and correct dimensions, then publishes prepared field R8 texture and CPU visibility. It never uploads, allocates or waits for GPU idle. An invalid two-phase contract terminates instead of allowing a half-published frame.
- modules/rts/view/RtsView.cpp: removed legacy early fog_.build() preflight. The existing Roads -> Shores -> Fog OwnerPrepareCommit cohort now stages Fog sheet and field together as its final participant, including grid-size changes. fogMeshDirty_ clears only on successful publication, not on rejected preparation.
- modules/rts/view/RtsView.h: setFogLook invalidates camera FramePublicationEpoch.
- tests/tests.cpp: added the Stage 46 late owner fault rollback test (same-size replacement) and staged map-resize GPU field test (no full GPU idle). Both exercise retirement counts and preservation of previous published field/texture.

## Verification honestly performed

- GitHub source fetched and edited against exact parent commit 84fa59052c0f0a6bec8fd86bcf99c45cb3846448.
- Mechanical single-occurrence replacement checks and text-level invariant checks were performed.
- Native C++ compile, CMake configure, CTest, Metal/Vulkan/GLES compilation and GPU parity tests were NOT RUN. These tests are written but UNVERIFIED.
- This patch is deliberately isolated to generic VisibilityField, the RTS host adapter and tests; no genre-specific rules enter the generic engine view component.

## Known risks and limitations

1. Legacy VisibilityField::build() remains in place for callers outside RtsView; it still uses an explicit GPU-idle replacement barrier. Convert those callers independently only after auditing their resource fences.
2. The two new tests are not compiled; check the fake Device's retirement accounting and any platform differences during the consolidated test pass.
3. CPU layout and R8 texture are swapped without backend draw on the owner thread, but owner code must not mutate VisibilityField between prepareAll and publish. Invalid revision is fail-closed.
4. The prepare path duplicates sheet CPU vertex/index memory and private GPU allocations. There is not yet a global per-frame staging memory budget.
5. Roads, Shores and Fog are one cohort, but Terrain/Detail, GroundFX, sprites, animation, lighting and overlays are still independent; entire RtsView remains non-atomic.
6. The legacy hot reload and device-loss generation paths still need integration tests with multi-camera secondary capture and resource retirement.

## Next stage — Stage 47

Externalize TerrainRenderer / Terrain Detail's already existing unpublished map build as a prepared candidate, then GroundFX, and register both in OwnerPrepareCommit before the currently published road/shore/fog owners. Add a last-participant fault which proves the full Surface + Fog group keeps old geometry, textures and camera epoch. Follow with MotionWeave immutable animation packets, sprites, lights and overlays. Continue one handoff per stage. Perform user's requested consolidated CMake/CTest and Metal/Vulkan/GLES validation after completing functions and fix all compiler/test failures before merging main.
