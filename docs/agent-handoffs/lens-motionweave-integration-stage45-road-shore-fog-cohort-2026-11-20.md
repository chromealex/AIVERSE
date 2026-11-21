# AIVERSE LENS — Stage 45: shared Roads + Shores + Fog prepare/commit cohort

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration; main NOT merged.
Stage 44 base HEAD: f4f8ec1336cb76b25851714cea8949fd0bd79257
Stage 45 source HEAD before documentation: 77e5a9bbc9192d198a85a1ae5ba03328f0cbc213
Prior handoff: docs/agent-handoffs/lens-motionweave-integration-stage44-road-transactions-2026-11-20.md

## Changes / commits

- f9aeebee1e8f235e107367fc519ca7658ba25d88 — RtsRoadView.h: external prepareRebuild / commitRebuild and dirty predicate.
- 9c9e6211c84cdff2885bb7c9c766b30d16bb6832 — RtsRoadView.h: cmath.
- 0cede106526b1da5f94b2866dc0b4840e0d03f8a — RtsRoadView.cpp: externally owned, self-cleaning prepared bundle, no-upload commit; legacy rebuild remains.
- 1a6093ba7ab273d5c5477789e7c9576d56b492bf — RtsShoreView.h: external prepared bundle API.
- c241dee325747ed5833a2309f8b080e48789774a — RtsShoreView.cpp: separate prepared Shore GPU bank/foam/prop bundle from commit.
- 9b7109a19101703472e4630dc35cd082550483d5 — RtsView.cpp: Road + Shore + Fog participants in one OwnerPrepareCommit before scene layer submission.
- 564bafb536bc189b55f8009141e4e7b775659f05, de58acb357dfb94d1c933002e5a29861c843dbb1 — RtsRoadView.h/.cpp: opaque shared_ptr candidate type across translation units.
- 1886c7b2618e348d2e8cbc44ab172e37084f640e, 1933100dd2141663518426a3784995255f5ab0bf — RtsShoreView.h/.cpp: same ABI correction.
- 5283bb2a14aba1bf54bec30348eb5bfd01927e88 — RtsView.cpp: own candidates with incomplete-type-safe shared_ptr.
- 7882a2da01b1862430ff9b781307912bd4e40e20 — tests/tests.cpp: late Fog failure after real Road and Shore staging.
- a60a2d72cba7afb2ea6060d7e6083220a3744978 — RtsView.cpp: explicit exception header.
- 3bf923e3ae89773fc700264f642f122e8df0682f — RtsRoadView.cpp: weather/preview-only staging avoids rebuilding road geometry or advancing wheel-surface generation.
- 5f149d6c931297fbf2acbdb5ed9541764a637010, 1accd350290019c0618baed8eb4f6c4f42f30c3e — Road and Shore duplicate commit guards.
- 77e5a9bbc9192d198a85a1ae5ba03328f0cbc213 — tests/tests.cpp: weather-only rollback/retry and single-commit test.

## Architecture and public API

The existing genre-independent engine/render/OwnerPrepareCommit.h coordinator runs prepareAll() before publish(). A failed late prepare runs rollback in reverse owner order. Participants promise noexcept commit and noexcept rollback and must not mutate their published resources during prepare. GPU retirement remains completion-token driven by the existing NativePresentationRetirement queue.

Road API in modules/rts/view/RtsRoadView.h:
- needsVisualPreparation() const noexcept
- shared_ptr<PreparedRebuild> prepareRebuild(gfx::Device&, heightAt)
- commitRebuild(shared_ptr<PreparedRebuild>) noexcept

Road prepare copies GPU kit references as borrowed, builds unpublished Models, decals, wheel-surface samples, new painted variants and weather meshes; candidate destructor releases only new GPU allocations. Full-road commit transfers all prepared data and defers old resource retirement. Weather/ghost-only edits prepare replacement film/puddles/preview, do not alter geometry, do not invalidate cached road wheel heights and do not advance surfaceGeneration. A committed shared candidate is guarded against repeat commit. Standalone Road::draw/rebuild remain available to editor consumers.

Shore API in modules/rts/view/RtsShoreView.h:
- needsVisualPreparation(waterY,width,height) const noexcept
- shared_ptr<PreparedRebuild> prepareRebuild(gfx::Device&, heightAt, waterY,width,height)
- commitRebuild(shared_ptr<PreparedRebuild>) noexcept

A Shore candidate privately owns new bank, foam and prop Models, while borrowing the still-live material kit. GPU upload errors leave previous shore Models intact. Successful cohort commit retires old models then publishes the prepared set with no new GPU uploads. Duplicate shared candidate commit is a no-op.

Fog is the engine-level VisibilityField from Stage 43. prepareUpdate builds new private faded/blurred CPU field plus an R8 GPU texture. commitPreparedUpdate publishes the generation and queues previous texture for ordered retirement. The callback fails closed on unexpected same-thread revision mismatch.

RtsView::draw now builds an OwnerPrepareCommit with Road, Shore and Fog participants in that exact order. It executes prepareAll() then publish() BEFORE submitting any scene geometry/layers. The previous late Fog update has been removed: the visibility layer draws/attaches the already committed Fog field. A late Fog GPU allocation/upload failure cancels both earlier staged owners and Stage 42 FramePublicationEpoch blocks secondary cameras from consuming the invalid primary frame.

## Tests written — NOT RUN

1. lens_road_shore_fog_cohort_late_fog_failure_preserves_all_published_owners: fake RHI with a published asphalt road, seaside embankment and Fog R8 image; mutate Road/Shore sources, stage both, intentionally fail Fog upload last. Verify old road generation and wheel-surface samples, Shore retirement count, Fog GPU texture and all published pending retirement counts remain unchanged; retry and assert all three publish and old resources retire.
2. lens_road_prepared_weather_only_preserves_wheel_surface_generation: publish existing road then change wetness/puddles, discard one private candidate, retry, publish, ensure unchanged road generation, wheel-surface height and geometry stats; try duplicate candidate commit, expect no duplicate retirement.

Prior Stage 37–44 tests remain in tests/tests.cpp.

## Verification actually performed

Read current RtsRoadView.h/.cpp, RtsShoreView.h/.cpp, RtsView.cpp, VisibilityField.h/.cpp, NativePresentationRetirement.h, OwnerPrepareCommit.h and existing tests. Re-fetched source and verified opaque candidate ABI, RAII candidate cleanup, prepare-before-publish ordering, Road weather-only fast path, one-shot commit guards, Fog last in the cohort, no extra late Fog update and existence of both tests.

NOT RUN: any native C++ compilation, CMake configure, aiverse_tests/CTest, Metal/Vulkan/GLES backend builds, real GPU fault injection or multi-camera parity. All test assertions remain hypotheses to validate in the requested single consolidated test run; static checks are not PASS evidence.

## Limits / work still open

This is now a shared update transaction for Roads + Shores + Fog FIELD TEXTURE, not a full RtsView cross-owner rollback. Terrain, GroundFX, sprite/MotionWeave animation, lighting and overlays continue to publish independently later in RtsView::draw. If a later owner fails, the already committed Road/Shore/Fog state can still change while Stage 42 only prevents replay of that invalid frame by secondary cameras.

Fog HEIGHT-SHEET build when fogMeshDirty_ is still performed as a preflight before the three-owner prepare. The old sheet uses its own GPU-idle gated publication and is not yet externally staged. Full all-Fog frame atomicity requires a prepared sheet mesh/texture bundle.

CPU scene/animation obstacle setup can precede the cohort. The old animation clocks are not fully restored on later draw exceptions. The Road candidate may duplicate full world CPU/GPU resources; it has no strict global frame memory limit. Some GPU backends may refuse or quarantine incomplete Model uploads. Real source lifetime, resource quotas and Visual RT parity remain unverified.

## Precise next stage (Stage 46)

Move Fog sheet geometry into a prepared resource candidate and join it to the cohort. Introduce prepared/commit APIs for Terrain and GroundFX, and integrate their state into the same pre-render transaction. Then migrate immutable sprite and MotionWeave animation frames, lighting and overlays. Add a fault-injected final-owner rollback test with no partially published complete frame. Preserve genre-neutral common render APIs, append a separate handoff after every stage, and defer full CMake/CTest and Metal/Vulkan/GLES validation until all functions are completed, as requested.
