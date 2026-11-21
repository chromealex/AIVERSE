# AIVERSE LENS Stage 36 — deferred Terrain/LOD/GroundFX mesh retirement and staged GPU uploads (2026-11-20)

## Repository, branch and exact code commits
- Repository: `chromealex/AIVERSE`, branch: `feature/lens-motionweave-integration`. No changes merged to `main`.
- `ef7e74ca0db1d42ebaea9fba2e097b59c1c8665d`: `modules/rts/view/RtsTerrainRender.h` — `NativePresentationRetirement retiredMeshes_`, public pre-render `collectRetiredMeshBuffers(dev)` and `pendingMeshGpuRetirements()`.
- `88e3b7e6b361f849f3576089a114b5dc8178c50c`: `modules/rts/view/RtsGroundFx.h` — the same per-owner retirement queue and pre-render API.
- `1200e0dddf87257533db0fcff3b0bcb347687ed1`: `modules/rts/view/RtsTerrainRender.cpp` — stage replacement terrain vertex/index buffer handles before retiring published handles, route replaced chunk/LOD/apron/evicted buffers through the retirement queue, drain at final shutdown only after proven GPU completion.
- `fdb432e2765ae2cf20234804f8e31085c1452e48`: `modules/rts/view/RtsTerrainRender.cpp` — build replacement water mesh first and publish/retire old VB/IB only after upload succeeds. Preserves existing water mesh on upload exception.
- `9f43c8165d791126610bf5cd4efea8828c9a1fc2`: `modules/rts/view/RtsTerrainDetailMesh.cpp` — use GPU retirement instead of immediate buffer destruction on detail LOD invalidation.
- `85dfc46d6338c70262b8ced7c1ef727fc86f3e9a`: `engine/render/MeshBuilder.cpp` — shared MeshBuilder GPU upload cleans unpublished vertex/index handles if either allocation fails or throws, instead of returning a partially valid mesh.
- `d5eae65ba370e8e6df4cc1d4f26532dc87a05dc3`: `modules/rts/view/RtsGroundFx.cpp` — stage new GroundFX mesh before retiring old VB/IB; process GPU retirement queue at shutdown, abort before releasing shader/texture if idle cannot be proven.
- `54745a64980c5172e994b03ab3f63ecad9f03dc1`: `modules/rts/view/RtsView.cpp` — include Terrain/GroundFX queues in `pendingModelRetirements()` and collect both at the explicit pre-render resource-retirement boundary.
- `0d5d4bd16a8c7a4ea797c64a94c8cbfd15bdfbe6`: `modules/rts/view/RtsTerrainRender.cpp` — build all LODs of each changed non-detail chunk in a private `Chunk` and commit bounds/hash/handles only after success. On failure destroy only new unpublished handles; the old chunk remains published.
- `8737ad7d52c3321a87f947c81954fed07adaca59`: `tests/tests.cpp` — add `mesh_builder_upload_releases_unpublished_vertex_buffer_on_index_failure` and `terrain_chunk_rebuild_preserves_old_gpu_mesh_on_index_failure`.
- Previous stage handoff: `docs/agent-handoffs/lens-motionweave-integration-stage35-terrain-groundfx-kit-2026-11-20.md`.

## Design and API
1. Terrain and GroundFX each own a generic `render::NativePresentationRetirement` queue. Every replaced or invalidated old VB/IB is enqueued without assuming a frame age means GPU completion. The host polls both queues only at the render-owner pre-render frame boundary. Backends with ordered tokens release completed work; shutdown performs an explicit full-device completion barrier before releasing remaining GPU resources.
2. `TerrainRenderer::upload(dev,vertices,indices,out)` privately creates both VB and IB. On partial failure, destroy only never-submitted new allocations and throw; the existing `Lod out` is unchanged. On success retire the old buffers and publish the new pair and counts.
3. For the ordinary non-detail chunk rebuild, the entire set of the chunk's LOD buffers, bounds and new hash is staged in a fresh `Chunk next`. If the second or later LOD fails, clean its new, unpublished buffers and leave the currently published chunk unchanged, so the next build retries rather than silently skipping due to a stale updated hash.
4. `buildWater` now builds a new `Mesh` before modifying published water. The previous water mesh is retired only after a successful upload; removing water intentionally retires previous water buffers.
5. `GroundFX::setMesh` now calls `MeshBuilder::upload` before calling `freeMesh` on the prior mesh; `freeMesh`, including grid resets, enqueues both old buffers. Shared `MeshBuilder::upload` is responsible for cleaning failed private allocations. These meshes remain genre-view presentation only; no ECS gameplay state is updated.
6. `RtsView::collectRetiredModels` polls Terrain and GroundFX queues alongside Roads, Shores, Overlays, GeometryScene and existing model/image owners. This must remain outside recording an in-flight render pass.

## Tests written, not executed
- `mesh_builder_upload_releases_unpublished_vertex_buffer_on_index_failure`: inject index buffer allocation failure after a valid VB, expect exception and one private VB destruction; retry succeeds with both handles.
- `terrain_chunk_rebuild_preserves_old_gpu_mesh_on_index_failure`: build a 4×4 terrain, mutate a static rock flag, inject an index allocation failure on replacement, check the live scene/chunk remains built and no old buffers retired, then retry with allocation restored and verify new retirement entries and completed shutdown.
- Both tests are in the existing `tests/tests.cpp` using `fogfaulttest::Device`, a synchronous CPU recording fake, not a real GPU.
- No new CMake executable target; use the existing `aiverse_tests` registration.

## Actual validation
- Inspected latest GitHub branch `RtsTerrainRender.h/.cpp`, `RtsTerrainDetailMesh.cpp`, `RtsGroundFx.h/.cpp`, `RtsView.cpp`, `MeshBuilder.cpp`, `NativePresentationRetirement` and existing test harness.
- Successfully committed source files and re-fetched the modified files to check staging/commit order, retirement paths and test names.
- **NOT RUN**: CMake configure, native C++ compile, `aiverse_tests`, CTest, backend Metal/Vulkan/GLES build, GPU device tests, pixel parity, profiling. Do not claim compile success or passing tests.

## Important limitations and next actions
1. **A full map terrain rebuild is not yet atomic.** When terrain dimensions/layer/LOD mode change, `TerrainRenderer::build` can discard old active chunk containers before all replacement chunks are prepared. The newly introduced GPU retirement queue protects GPU-live buffers against immediate release, but does not by itself preserve the previous full map's presentation state if a later allocation fails. Implement outer whole-map staging/publish next.
2. **Detail terrain rebuild is not fully atomic.** Detail LOD invalidations use deferred retirement, but rebuilding its CPU height/splat state and multiple LOD buffers can still partially publish before an exception. Audit `RtsTerrainDetailMesh.cpp` and associated textures.
3. **GroundFX is not fully frame-atomic across all meshes.** One call to `setMesh` is staged, but an operation that updates multiple chunk meshes can publish earlier ones before a later upload fails. Add a batch-level transaction or journal rollback for clustered layer updates.
4. Terrain `cellTex_/dynTex_/splatTex_/visionTex_/detailNrmTex_/detailSplatTex_` and GroundFX dynamic icon/atlas updates need their own lifetime audit; mesh retirement does not cover textures or device loss.
5. A device permanently lost without GPU idle proof must retain ownership of GPU-live resources; draining or destroying the renderer without the RHI owner lease is unsupported. Add generation-aware cancellation/recovery as needed.
6. The full `RtsView` reload is still not atomically published across all owners. Latest MotionWeave `feature/universal-ecs-animation-pipeline` remains to be reconciled with a true merge-base and per-file conflict resolution; do not overwrite animation or editor changes.
7. After the remaining functional work, perform the user's requested **one consolidated native build + CTest** and supported Metal/Vulkan/GLES GPU/backend checks, fix discovered compile/regression failures, and only then consider merging into `main`.

## Suggested Stage 37
Transactional full-map Terrain chunk rebuild (including apron/water), failure injection for second/late chunk, and retaining old map presentation until full replacement. Separate handoff required.
