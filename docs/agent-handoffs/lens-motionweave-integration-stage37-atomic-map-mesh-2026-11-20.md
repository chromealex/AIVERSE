# AIVERSE LENS Stage 37 — atomic non-detail full-map mesh publish (2026-11-20)

## Repository, branch and commits
- Repository: chromealex/AIVERSE
- Branch: feature/lens-motionweave-integration. Not merged into main.
- Source commit: c4ab82c134505b59853eeb7c4ce537ff2f55cc47 — modules/rts/view/RtsTerrainRender.cpp
- Test commit: d19b7fbffcc82e041741e14284d7d5126caa2d6b — tests/tests.cpp
- Previous handoff: docs/agent-handoffs/lens-motionweave-integration-stage36-mesh-retirement-2026-11-20.md.

## Implemented
1. TerrainRenderer::build detects a full-map reconfiguration using the existing global hash, dimensions, heightfield and LOD mode. For non-detail terrain only, it creates a private TerrainRenderer staging owner with the same device-generation lease, look and topology.
2. All replacement chunks (including all LODs), the apron and optional water mesh upload to unpublished GPU handles first. Old live buffers are not retired until every staged upload succeeds.
3. Failure in any late chunk, apron or water upload destroys only never-submitted staged buffers, restores previous level height / heightfield / LOD metadata, and rethrows. Old published chunk/apron/water, global hash, map dimensions and stats are retained.
4. On success old buffers go to NativePresentationRetirement, then new chunk collection, apron, water status and map dimensions/hash are published. Deferred retirement is still polled at the existing owner pre-render boundary.
5. No genre-specific camera state added; immutable LENS per-view visual snapshots, ECS cameras and RenderTexture/capture contracts are unchanged.

## Tests authored (NOT run)
New case: terrain_full_map_stages_all_chunks_apron_and_water_before_publish in tests/tests.cpp.
- Publish a 4x4 map; attempt to replace with a 65x4 map (three chunks) with one deep water cell.
- Fault on second chunk index upload. Assert old one-chunk map stays built, no published buffers retire, exactly three unpublished buffers destroy, and previous map/hash still works.
- Fault on water index upload after three chunk pairs and an apron pair. Assert nine unpublished buffers destroy, with old map and pending retirements unchanged.
- Retry successfully, assert three chunks and deferred old chunk + apron retirement, then successful shutdown.

## Actual validation
- Inspected the Stage 35/36 handoffs, RtsTerrainRender.h/.cpp, RtsTerrainDetailMesh.cpp, RenderDeviceLifetime.h, tests/tests.cpp and fogfaulttest fake RHI.
- Committed implementation and regression test through connected GitHub.
- NOT RUN: CMake configure, C++ build, aiverse_tests, CTest, real Metal/Vulkan/GLES compile, GPU tests, pixel-parity, performance profiling. The newly authored tests are not proof of a passing build.

## Known limitations and next actions
1. The transaction currently covers full-map MESH upload, not entire GPU presentation. updateCellTextures / updateDynamic run after mesh publication. In particular map-resize still destroys old cell/dynamic/splat/vision textures in place, so texture failure may leave mixed state. Stage 38: build replacement GPU textures + CPU pixels privately and publish texture handles with geometry; retire old textures only after GPU completion tokens. Inject late texture allocation/upload failures.
2. Detail terrain remains non-atomic: detail CPU height/splat, multiple LODs, normal/splat textures and hash updates need their own transaction.
3. Ordinary incremental changes across many chunks and grouped GroundFX setMesh updates still may partially publish. Use a batch transaction or journal rollback where frame-atomicity is required.
4. RtsView-wide reload across multiple owners is not yet atomic; reconcile current MotionWeave branch feature/universal-ecs-animation-pipeline via actual merge-base and per-file conflict resolution.
5. Device loss without a proven GPU idle barrier requires persistent RHI-owner lease and deferred-retirement safety. No age-based destruction.
6. Stage 37 assumes build runs on presentation owner thread outside camera render/capture recording. Staged buffers are never submitted and may be destroyed immediately on a staging failure, while old published buffers require deferred retirement.
7. After finishing feature work, perform the requested SINGLE consolidated CMake/CTest run plus supported Metal/Vulkan/GLES compile checks, fix failures and only then consider merging into main.

## Next exact stage: LENS Stage 38
Atomic per-map terrain texture replacement, old-texture retirement, fault injection and integration with staged map geometry. Add a separate agent handoff at the end.
