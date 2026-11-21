# AIVERSE LENS Stage 47B.3A — camera-visible fine Detail LOD staging

Date: 2026-11-20
Repo: chromealex/AIVERSE; branch: feature/lens-motionweave-integration; main not touched.
Parent: 9d6b26178d9cfa1c74f7257db3b9eb1f67b65093
Prior: docs/agent-handoffs/lens-motionweave-integration-stage47b2-terrain-dynamic-vision-cow-2026-11-20.md

## Changed source/API
- Added TerrainRenderer::prepareFrameVisible(Renderer3D, MapGrid, vision, filteredFog). It reuses the Stage47B.2 PreparedFrame dynamic/vision COW candidate, and stages up to 16 missing camera-visible fine Detail LOD GPU vertex/index buffers in a private PendingLod journal.
- buildDetailLod accepts optional stagedBytes accumulator, so private LOD generation does not modify live geoBytes_. Old chunk GPU handles remain borrowed; only one empty target LOD slot on a CPU chunk clone is uploaded. An abandoned candidate cleans up its own newly allocated buffers.
- commitFrame publishes staged LOD handles and geometry byte accounting atomically with the terrain field textures, only after GroundFX/Road/Shore/Fog prepare passes.
- Primary RtsView uses prepareFrameVisible. When stagedFields is true, TerrainRenderer::drawInternal no longer uploads missing fine LODs during draw; instead it uses a resident coarser/fallback LOD as secondary cameras already do. Standalone clients retain existing lazy LOD behavior.
- New test lens_stage47b3a_detail_lod_candidate_rollback_and_submission_only_draw checks rollback, old retention and lack of late LOD upload.

## Actual verification
GitHub source inspected and mechanically edited. C++ compilation, CMake, CTest, Metal/Vulkan/GLES builds, GPU parity, benchmarks NOT RUN. Tests added but UNEXECUTED.

## Remaining risks/next
- Terrain primary draw still can create a material shader/pipeline or traced-height GPU texture after the owner cohort. Stage47B.3B should prepare these into the same candidate and publish without allocation.
- Map topology and same-map sculpt edits publish earlier from RtsView::buildTerrain; unify with dependent GroundFX and fog later.
- Up to 16 fine LODs are prepped per frame, and missing LODs beyond that cap fall back to resident levels until a later frame. Validate visual quality/memory/perf.
- After features: consolidated native CMake/CTest, all supported GPU backends, regression repair before merging main.
