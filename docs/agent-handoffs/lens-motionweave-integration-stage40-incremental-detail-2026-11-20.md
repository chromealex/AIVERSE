# AIVERSE LENS Stage 40 — incremental dirty-region Detail Terrain transactions (2026-11-20)

## Repository / branch / exact changes
- Repository: chromealex/AIVERSE
- Feature branch: feature/lens-motionweave-integration. Nothing merged into main.
- Stage 39 base: 878987bc89a7fbc5a5667ea35e6f85faec0252ea
- e87dc97478be292586b3c7a6f0c3e0f12f1f7897 — modules/rts/view/RtsTerrainRender.h: private tryBuildDetailIncremental entry point.
- b788fdfcb7d7ebd5166bf0a56b0f0b2b2bc7fa2f — modules/rts/view/RtsTerrainDetailMesh.cpp: copy-on-write normal/splat CPU texel cache, update only dirty region where sizes match.
- a0580bbd5a1144013388e58103ae8cdf7554531b — modules/rts/view/RtsTerrainDetailMesh.cpp: transactional same-map detail updates.
- 2337bc9f292538a6f9e6464176c399c1b9c1f4d1 — modules/rts/view/RtsTerrainRender.cpp: incremental path before Stage 39 full transaction, rollback of changed preflight metadata on exception.
- 400f6bc9e2f276ee9eb08f53b6c5bc6f266db074 — tests/tests.cpp: two new fault-injection/perf-shape regression tests.
- f9c9076a8b2c395ab30815663e926128b30b8086 — modules/rts/view/RtsTerrainDetailMesh.cpp: explicit utility header for handle exchange.

## Actual functionality
1. Same-size Detail Terrain edits are classified using existing published per-chunk hash, the pending explicit detail dirty-sample rectangle, active texture dimensions and current quality-dependent detail resolution. Global remap, changed scale, changed material-layer assignments or invalid cache use the existing Stage 39 complete staged rebuild.
2. For small dirty edits, build() calls tryBuildDetailIncremental(). It creates a private staging owner, clones the CPU detail surface plus cached per-detail normal/splat texels, carries only metadata of all existing chunks, and zeroes every copied GPU handle. Unchanged published chunk LOD buffers are never owned by the staging instance.
3. buildDetail(false) resynthesizes/replaces only the existing changed sample rectangle and updates per-chunk hash/height bounds. The new updateDetailTextures preserves cached clean pixels for matching dimensions, recomputing only the dirty texel rectangle. Explicit splat-only edits force texture refresh even if all height hashes are identical.
4. New cell/dynamic/splat/vision and normal/splat GPU textures are private generations, then staged water and coarse detail LODs are uploaded only for chunks whose hash/height/bounds changed. The unchanged live chunk mesh handles remain intact; apron and unrelated fine LODs are not rebuilt.
5. On *any* staging exception, only unpublished new texture and mesh handles are destroyed, and the caller restores level-height, heightfield and LOD preflight state. Old surfaceAt() height, published geometry, pending GPU retirements and GPU bindings are unchanged. Pending edit source/dirty markers remain for retry.
6. On success, changed old chunk buffers and all six old texture generations plus old water are handed to the existing NativePresentationRetirement queue. The new CPU surface and GPU textures publish together. Unchanged chunk LOD handles remain in the active map. Stats.rebuilt records the number of chunks whose GPU geometry was replaced.
7. Fast path guards: at most 16 estimated dirty chunks and an estimated 128 MiB transient staging budget (CPU height/pixels + fresh GPU images + coarse LOD allowance). Over-budget or global changes fall back to the existing full-map Stage 39 transaction; THIS IS NOT a hard total-memory cap for all Detail Terrain builds.
8. No genre-specific camera change; the Stage 39 per-camera immutable render packet and completion-token ownership remain in use for RT/capture and multi-camera views.

## Tests authored — NOT executed
- terrain_detail_incremental_rebuild_only_uploads_dirty_chunk_and_rolls_back:
  - 65x4 smooth map with 3 GPU chunks.
  - Change one cell near the first chunk, fail the dirty coarse LOD index allocation, assert only the private VB was released and no published buffers retired.
  - Retry; assert exactly one 2-buffer coarse chunk is uploaded, Stats.rebuilt==1, and an unchanged subsequent build allocates zero extra buffers.
- terrain_detail_incremental_explicit_splat_edit_keeps_all_chunk_buffers:
  - Explicit 65x4 detail surface and RGBA splat data.
  - Edit a single splat sample without changing any height, fail a staged texture update, assert no chunk meshes uploaded and no published resource retired.
  - Retry; expect zero changed chunks and zero mesh buffer allocations; completed GPU retirements happen at shutdown.
- Stage 37–39 regression tests remain present in tests/tests.cpp.

## Validation actually completed
- Used connected GitHub to inspect latest implementation, affected headers and existing tests.
- Re-fetched changed files and checked that the private incremental method is declared, called, bounded and implemented; that unchanged GPU handles are excluded from staging and cleanup; and that both regression test names exist.
- Source and tests committed to the feature branch.
- NOT RUN: CMake, native C++ compilation, any aiverse_tests/CTest, Metal/Vulkan/GLES builds, actual GPU tests, pixel parity or performance benchmarking. The exact expected test results above remain assertions TO BE VERIFIED by the user's deferred single validation run.

## Known limitations and upcoming work
1. The 128 MiB / 16-chunk check protects only the *incremental* path using a conservative byte estimate; the full Stage 39 fallback still may double map-sized CPU/GPU allocations. A hard total budget requires resumable bounded staging across frames or temporary source-image remapping.
2. Dirty-region normal/splat pixel recomputation is incremental, but copy-on-write GPU textures still allocate whole images from cached pixels. This is needed to avoid mutating images currently sampled by in-flight cameras; virtual texture region subresources or GPU blit/copy-on-write atlas pages would reduce full texture upload cost.
3. Dirty-region staging still copies whole CPU DetailSurface and cached pixel arrays once. A persistent immutable chunked/tiled sample cache would remove these copies.
4. Water is currently staged as one map-wide mesh on any detail update; optimizing it needs water-body dirty tracking and coordinated transactions.
5. The fast path excludes painted material layer changes and source detail dimension/scale changes; those correctly take the full transaction.
6. Same-size dynamic field/creep updates in build's unchanged detail path still update GPU textures in place. Simultaneous cross-camera pixel-generation determinism needs an immutable per-view dynamic texture scheme.
7. Validate later that synthetic detail editing far from boundaries changes exactly one chunk; the new test asserts this but HAS NOT BEEN RUN.
8. All owners of RtsView still need batch atomic GroundFX publication and a conflict-aware MotionWeave merge. DO NOT overwrite editor/animation branches.

## Suggested next Stage 41
Transactional batch publication of GroundFX meshes with bounds on temporary GPU memory; add two-chunk late-failure tests and preserve per-camera packet ownership. Then audit cross-view epoch and resource retirement. Keep one handoff file for every stage. Do the user's requested single comprehensive CMake/CTest validation and backend compilation after completing functions, not now.
