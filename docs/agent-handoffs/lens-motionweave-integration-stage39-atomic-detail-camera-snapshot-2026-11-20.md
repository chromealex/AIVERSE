# AIVERSE LENS Stage 39 — transactional Detail Terrain + immutable per-camera Terrain GPU bindings (2026-11-20)

## Repo and commits
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (not merged to main).
Starting HEAD: 94b473270595184a45f9c0a5c5478dc5f559512e (Stage 38).
Source commits:
- e5398d1405da9e149694b66232e38545e1beafa6: RtsTerrainRender.h material layer edit dirtiness.
- 2992228f192d468b1b338ab96d5db3f47ecd8e66: RtsTerrainRender.cpp atomic staging for complete Detail Terrain surface, normal/splat textures, cell textures, water, apron and coarse LODs.
- 34a5d27ead1e21c127460398a7977909631bab97: resolve terrain material kit before map publication, retire abandoned detail GPU textures when switching to non-detail mode.
- 220beceee0ac62947b6f67ffa51e91a328e632a3: RtsTerrainDetailMesh.cpp reject invalid normal/splat GPU allocations.
- ff73d807a3f885421cd1bf7f4854e2c3874c300e: tests/tests.cpp fault-injection regression test for Stage 39.
- ae50500accc307b9c354f7e43abd3aa64f3011d8: RtsTerrainRender.cpp immutable terrain LOD/texture bindings in deferred camera draws.
- f666bf3385c9240de8c928d5906bed6bbdb75dbb: RtsTerrainRender.cpp trace height field GPU texture replacement with deferred retirement.
- e1636c91c707dcb2ef60da2004c23ac664ce068a: explicit stdexcept include for detail allocation failures.
Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage38-atomic-map-textures-2026-11-20.md.

## Implemented architecture
1. TerrainRenderer::build now checks whether detail surface must be reconstructed: map/global hash mismatch, missing built detail, explicit file detail changes, edited sample bounds, changes to painted material layers, or changed chunk inputs. If none changed it retains the light path (cell/dynamic texture uploads only), with no new LODs.
2. For changed Detail Terrain, a private TerrainRenderer staging owner copies the current look, painted material layer, explicit file detail/splat source, device-generation lease and map dimensions. It synthesizes or resamples the new DetailSurface, builds the detail normal/splat images, the four cell textures, outer apron, water and dynamic fields without modifying any of the published map geometry or GPU texture handles.
3. To guarantee immediate secondary-camera fallback, one compact coarsest detail LOD per chunk is GPU-uploaded in the unpublished staging owner. Fine LODs remain lazy, following the previous primary-camera behavior.
4. Any exception at CPU generation, texture creation/update, apron/water creation, or a late chunk LOD index-buffer upload causes the staging owner to destroy only unpublished buffers/textures and rethrow. Old chunk topology, surfaceAt height, GPU textures, water, stats and pending retirement state remain published. Temporary heightfield, level height and LOD metadata are restored.
5. On full staging success, the old published detail meshes, apron, water and all SIX map/detail texture handles are queued for ordered GPU retirement; the entire replacement CPU surface, GPU handles, pixel arrays and chunks are then published. Edited-file/detail/material dirty bits clear only after success.
6. Shutting TerrainRenderer down now destroys active detail normal/splat textures too, after the existing GPU idle/retirement completion. Switching back to non-detail mode explicitly retires old detail textures and clears abandoned detail arrays/metadata.
7. Deferred Terrain Renderer3D drawCustomOpaque callbacks now store Lod values, a copy of the apron Lod and all cell, dynamic, splat, vision, detail normal/splat, layer and cliff texture handles in an immutable command snapshot. No callback reads live mutable TerrainRenderer texture fields or dangling pointers into chunks_. These snapshots are genre-neutral and support primary + secondary LENS RT/capture cameras.
8. Trace height-field texture regeneration now stages a fresh GPU texture instead of overwriting or destroying the old image in place. An invalid new allocation raises an error, leaves the old shadow texture active and retains the dirty bit for retry. The old image is retired through the same completion-proven queue after replacement.

## Test authored, NOT RUN
TEST(terrain_detail_stage_rolls_back_late_lod_and_normal_splat_failures) in tests/tests.cpp:
- Parse a smooth terrain visual look with detail scale 2. Publish a 4x4 map; assert one active detail chunk with zero pending retirements.
- Attempt a 65x4 three-chunk replacement and inject failure at detail splat GPU texture allocation. Expect five never-submitted textures destroyed, no old GPU retirement, original height retained.
- Retry and fail the second detail texture pixel upload. Expect all six staging textures destroyed, old map still active.
- Inject index-buffer failure for the third coarse chunk after the apron and two earlier coarse chunks. Expect seven staged buffers destroyed, old published buffers intact and zero retirement.
- Retry successfully, expect three detail chunks and old resource retirement.
- Edit the existing map without resizing; inject a late detail-splat GPU upload failure and assert previously published geometry, height and retirement counts stay intact. Retry and shut down cleanly.
- Uses existing fake fogfaulttest::Device. This is a CPU fake RHI test, not a GPU parity test.

## Verification actually done
- Read RtsTerrainDetailMesh.cpp and RtsTerrainDetail.h, RtsTerrainRender.cpp/.h, current tests/tests.cpp and the Stage 38 handoff.
- Published source and test commits to the feature branch.
- Re-fetched latest files and checked source anchors for transaction, texture retirement, complete staged coarse LOD and immutable draw-command bindings.
- NOT RUN: any CMake generation, native compiler, aiverse_tests execution, CTest, Metal/Vulkan/GLES build, GPU runtime, pixel parity or performance profiling. Do not treat static checks as passing tests.

## Known limitations / open work
1. Stage 39 prioritizes correctness: every changed detail source reconstructs the whole detail surface and all map/detail images, plus one coarse LOD per chunk. This can be expensive on large maps and temporarily doubles CPU/GPU memory. Stage 40 should optimize dirty-rect and dirty-chunk transactional staging while preserving identical failure rollback semantics and retirement fences.
2. Fine detail LODs remain lazily uploaded on primary draws. Per-frame LRU eviction and budget interactions are not yet independently tested with multiple RT cameras; add coverage for secondary camera fallback under heavy memory pressure.
3. The traced height map still has a 0.3-second regeneration throttle; same-size detail edits may temporarily use the previous shadow height texture. Snapshot lifetime is safe, but strict same-frame visual parity and timing need testing.
4. The unchanged detail fast path still updates live per-cell/dynamic textures in place; field/creep changes do not yet have an immutable per-camera image generation. Generalize the pixel snapshot where necessary for simultaneous RT captures.
5. Terrain's whole-map staging does not make RtsView's reload of all other view owners atomic. GroundFX batch mesh updates and multi-owner render reload still need coordination; apply the same owner lease and completion-token rules.
6. Staged full-map non-detail updates (Stages 37/38) are separate from Stage 39 detail mode and should receive the same deferred draw snapshot treatment (now done for the shared Terrain draw command).
7. The owner must call build only at the supported pre-render publication boundary and process retirement only after GPU submission/completion semantics. The draw callback retains captured GPU handle values, but that is not a substitute for correct RHI retirement ordering.
8. Latest MotionWeave branch integration still requires actual merge-base/conflict-aware reconciliation preserving animation, editor and RtsView changes.

## Next exact stage
Stage 40: incremental dirty-region Detail Terrain transactions and bounded GPU/CPU staging budget; then grouped GroundFX updates and full multi-owner reload. After all feature work, one consolidated native CMake/CTest validation and supported Metal/Vulkan/GLES builds, with failures fixed before merging main. Continue writing one handoff per stage.
