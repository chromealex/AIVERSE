# AIVERSE LENS — Stage 48B.3B.0: fail-closed topology source/revision validation

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: `feature/lens-motionweave-integration` (**main untouched**)
Parent commit: `dbad53035f80c96ceb11f0c998115164112425ba`
Previous: `docs/agent-handoffs/lens-motionweave-integration-stage48b3a1-terrain-unbuilt-planet-height-fallback-2026-11-20.md`

## Implemented

The opt-in `RtsView::buildTerrainAtomic()` Terrain → Roads → Shores → GroundFX → Fog transaction now captures the same conservative MapGrid byte fingerprint that the LENS scene prewarm lease uses. It also captures all five published-owner state revisions plus the terrain-policy generation BEFORE staging any GPU candidate. Immediately after `OwnerPrepareCommit::prepareAll()` — and before any owner is allowed to publish — it compares current inputs against the originals.

If a same-address map is edited by an editor/RHI callback during staging, if an owner is reconfigured/replaced, if the terrain generation changes, or if the device/planet mode changes, it explicitly rolls back the transaction in reverse owner order and throws a `std::runtime_error` describing the source change. Live height mesh, road/shore generations, GroundFX, Fog R8 and GPU retirement queue remain on the previously published versions. There is no partial commit.

This reuses `mapLeaseFingerprint` from `RtsView.cpp` through a private `RtsView::mapSourceFingerprint` wrapper, avoiding independently drifting definitions in terrain and frame preparation. This is an intermediate correctness gate, **not** a complete unpublished topology+scene lease.

## Files

- `modules/rts/view/RtsView.h`: private common map fingerprint declaration.
- `modules/rts/view/RtsView.cpp`: common fingerprint implementation.
- `modules/rts/view/RtsTerrain.cpp`: pre/post prepare fingerprint and owner-revision validation, rollback before publication.
- `tests/tests.cpp`: `lens_stage48b3b0_topology_discards_mid_prepare_editor_mutation`; fake RHI device mutates an in-place height corner in `createBuffer`, asserts rejection, intact previously published height/road generation/retirement queue, then stable retry.

## Verification (actual)

- Fetched source at exact parent SHA, applied unique-anchor edits, and committed the four sources and this handoff using GitHub Git tree objects and an expected-HEAD ref update.
- No CMake build, C++ compiler, CTest, Metal/Vulkan/GLES or GPU playback run in this environment. The new test is **authored but NOT executed**. Do not mark it PASS.

## Remaining work

1. Stage 48B.3B.1: introduce an explicit unpublished map-topology candidate across calls and enable scene-aware GroundFX visible-chunk preparation against the staged surface (without exposing pending RHI state). GroundFX currently resets CPU geometry first and fills GPU meshes only at next primary draw.
2. Stage 48B.3B.2: combine that topology lease with the existing prewarm scene lease, implement source/owner validation at consumption, including same-size Detail, and ensure failed prewarm never publishes either cohort.
3. Stage 49: Sprite/MotionWeave, lighting, overlays and 2D paint-layer publication.
4. One consolidated native build/CTest/fake-RHI suite and available Metal/Vulkan/GLES compilation + runtime parity/capture checks before main merge; update this document with actual results.

All five owner resources must remain genre-independent at the coordinator level. The LENS integration feature branch is not integrated into main.
