# AIVERSE LENS Stage 38 — stage and retire non-detail map textures with full-map meshes (2026-11-20)

## Repository and exact commits
- Repository: chromealex/AIVERSE, branch: feature/lens-motionweave-integration. main remains untouched.
- b97e29ebe4af6d6c809060924859add370ab85fe — engine/render/NativePresentationRetirement.h: generic ordered retirement for gfx::TextureHandle.
- ea61f79481f36f15079365723c4484b5887b92e1 — modules/rts/view/RtsTerrainRender.cpp: stage four map textures in full-map transaction, add rollback on partial GPU allocation and data upload failures.
- fe50b057ee2d7c5cb665263d137c7c49de01e178 — tests/tests.cpp: inject texture allocation and upload failures.
- Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage37-atomic-map-mesh-2026-11-20.md.

## Changes implemented
1. NativePresentationRetirement::Resource now holds an optional GPU texture handle. retireTexture(device, texture) transfers the ownership of an old published texture into the same async completion queue as buffers and models. The queue releases it only after the RHI proves ordered completion or a full device idle fallback. Failed proof never destroys GPU-live resources.
2. TerrainRenderer::updateCellTextures first allocates new CPU pixel arrays and four GPU textures locally. A failed second/third/fourth allocation destroys only never-submitted new handles and preserves the old published textures and dimensions. On success the replaced old GPU textures retire using ordered GPU completion instead of immediate destroy.
3. On full-map non-detail rebuild, the temporary TerrainRenderer from Stage 37 now has the current material-layer assignments and also calls updateCellTextures and updateDynamic before publication of any replacement chunk/apron/water.
4. If mesh, texture allocation, cell/splat pixel upload or dynamic field upload fails, the Stage 37 catch destroys all of the private staging buffers and textures, restores previous temporary geometry metadata and retains the previous published map, dimensions, GPU textures, global hash and retirement queue.
5. After every new upload succeeds, the old map buffers and all four old per-cell GPU textures join the retirement queue, the new mesh+texture handles and CPU pixel arrays move into the live map, and fog vision is marked dirty for refreshed camera views.
6. LENS ECS multi-camera, RenderTexture/capture, immutable CameraVisualFrame contracts, animation clock and game ECS state remain genre-neutral and unchanged.

## Tests written, not run
- Modified the fogfaulttest::Device fake to support failTextureUpdate and count updateTexture calls; no faults are active by default.
- Added terrain_full_map_texture_allocation_and_upload_failure_preserve_old_frame:
  - Seed a published 4x4 map and try a 65x4 replacement (three chunks, apron).
  - Reject third staged texture allocation: the two private textures and all eight private buffers must be destroyed; previous map still built and zero published retirements.
  - Reject the second texture update (splat): all four staged textures and eight private buffers must be destroyed; previous one-chunk map remains active.
  - Retry and assert three chunks plus at least eight deferred GPU retirements (old four textures, chunk VB/IB and apron VB/IB), then drain safely at shutdown.
- The Stage 37 late-chunk and water-index test remains present.
- Existing tests using fogfaulttest::Device default to no injected upload failure.

## Validation actually performed
- Inspected NativePresentationRetirement, Gfx Device::updateTexture signature, RtsTerrainRender.cpp, existing fake RHI and Stage 37.
- Created source and test commits in GitHub branch.
- NOT RUN: CMake configure, full native compile, aiverse_tests, CTest, real Metal/Vulkan/GLES builds, GPU execution, pixel parity and performance. Do not claim these passed.

## Risks / limitations
1. The new full-map staging transaction applies to NON-DETAIL terrain. Detail terrain reconstruction still publishes CPU detail heights/LOD/normal/splat state incrementally.
2. Initial/lazy material TextureCache::resolve and shader/pipeline creation are still outside the all-or-nothing full-map publication. Full RtsView reload across different owners also remains separate.
3. Existing same-sized non-detail incremental edits may update cell/splat GPU textures after some chunk meshes have published; grouped incremental frame-atomicity is not achieved.
4. updateCellTextures now stages GPU allocation before swapping on size changes, but in the non-staged detail path an exception from an actual updateTexture call after allocation can still leave partially published GPU contents.
5. NativePresentationRetirement is now generalized to textures but the legacy TerrainRenderer public diagnostic is still named pendingMeshGpuRetirements, and now counts textures too. Preserve compatibility until coordinated API rename.
6. Device-loss and wrong-device paths still require explicit owner lease and GPU completion proof. Never release possibly in-flight textures merely because a number of frames have elapsed.

## Suggested next Stage 39
- Transactional detail terrain CPU/GPU rebuild and per-detail texture generation; inject failure at late detail LOD and normal/splat texture uploads.
- Then group incremental non-detail chunk updates and GroundFX meshes under a frame-level transaction, consider common map presentation snapshot and unified owner reload, merge latest MotionWeave only by actual conflict-aware merge.
- Preserve an independent agent handoff after each stage.
- After remaining functions are complete, perform the user's requested single comprehensive native build + CTest and available Metal/Vulkan/GLES compile, fix failures, only then merge to main.
