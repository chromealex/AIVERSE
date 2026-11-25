# TerraWeave Caverns — stage 11: multi-chunk GeometryScene batching (2026-11-23)

## Provenance
Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`, originally forked from main and not merged. Stage 11 commits:
- `bfe08f69320af33f6a7d5b5e955a0b96ccbbd14e` — `engine/render/TerraWeaveCavernsBatchGeometry.h`
- `3b90328d5a2ac97bcaf95310a290ad48712cc596` — explicit dependency on cave streaming key comparator
- `3d8d11c60d273f065d9f6839d918629d38baa212` — `tests/terraweave_caverns_batch_geometry_test.cpp`
- `e50113b3a0fb642fa3fb37e5d953f923b7404eda` — stage 11 skill update
- This handoff commit follows these; check branch HEAD.

## Implementation
- `CaveBatchChunk` stores versioned source mesh plus chunk coordinates, resolution, spacing.
- `CaveBatchSpec` bounds chunks/vertices and opts into STRATA.
- `prepareCaveBatch` sorts chunk keys signed XYZ, refuses duplicate keys and invalid geometry, handles entirely empty chunks, checks a total vertex budget, constructs a single `Json` manifest and a source-keyed map of original unuploaded CPU `render::Model`s. Every instance position is an exact world-to-metre chunk-origin conversion for the chosen resolution and spacing.
- `CaveBatchGeometryResident` holds one actual `render::GeometryScene`, uploads all prepared sources through existing `GeometryScene::loadPrepared`, delegates drawing to `GeometryScene::draw` and teardown to `GeometryScene::shutdown`. Existing STRATA decides whether sources can use its portable render path. There is no second renderer.
- `tests/terraweave_caverns_batch_geometry_test.cpp`: tests order invariance of two chunks including negative coordinates, total vertex/triangle count, STRATA opt-in, duplicate rejection, budget rejection, and empty-chunk skip.
- Updated skill: `skills/world/aiverse-terraweave-caverns/SKILL.md`.

## Tests actually executed
- GitHub writes/updates reported successful repository commits.
- **No native compiler, CMake/CTest, GPU screenshot, real GeometryScene upload, STRATA selection or streaming runtime check was executed this stage.** Test code was authored only and must not be reported as passing.
- Example command requires full engine link dependencies, use CMake target when available; don't assume single-file compile can link Model/Json functions.

## Limits — critical
- Whole-batch replacement is still O(total geometry) staging, not fine-grained incremental source residency. Existing GeometryScene shutdown can require GPU idle and therefore may cause spikes.
- Old batch remains live until a new stage loads; cross-owner Renderer3D/SpatialWorld/Box3D/NavMesh atomicity is NOT implemented by this adapter.
- There is no actual LOD seam weld, Transvoxel transition, cluster-level cave edit propagation, or deep cave physics sync in this stage. STRATA visual simplification does not change authoritative geometry.
- GeometryScene can decline particular STRATA routes; enable it as an opt-in and inspect selection and hardware raster parity.
- A chunk may have no cave surface: skip its visual source but retain gameplay/streaming residency as needed. The API returns empty models when all chunks are empty; publish then returns false and does not implicitly remove existing scene.
- A batch replacement must not also be submitted by the per-chunk stage-10 resident or stage-5 clipped-terrain resident with the same geometry.

## Next steps
1. Implement incremental cave-source dirty replacement with actual GeometryScene residency and per-source GPU fenced retirement instead of full batch reload.
2. Add strict world-edge seam audit and LOD transition geometry between neighboring chunks (different resolution), verify coordinates, normals and winding.
3. Refactor stage-5/6 immediate publishers into a safe host prepare/commit barrier with rollback and Box3D/SpatialWorld/navmesh synchronization.
4. Build and run all stage 1–11 tests, debug source compatibility, run real GPU/STRATA/Box3D/cave-navigation acceptance.
5. Add editor brushes, authored cave edits and persistent storage; every stage gets its own handoff and skill update.
