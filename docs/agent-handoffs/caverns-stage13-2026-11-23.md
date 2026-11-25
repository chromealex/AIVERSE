# TerraWeave Caverns — Stage 13 handoff (2026-11-23)

## Provenance
- Repository `chromealex/AIVERSE`; feature branch `feature/terraweave-caverns-core-20261123`, originally created from main. No merge into main.
- `2c5595f97e4a85ba1366e234a680d49a4f32688d`: added `engine/world/TerraWeaveCavernsLodWeld.h`.
- `0c20b615c2328e1dfacf5153aa837464e25f82db`: added `tests/terraweave_caverns_lod_weld_test.cpp`.
- `bfc79335c16ce8497c26412660e5699df583ba80`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff follows; read latest branch HEAD for actual tip.

## Implementation
- `WeldResult weldCaveMesh(const CaveMesh&)`: deterministic first-appearance welding on integer-centimetre position keys, index remap, remove index-degenerate triangle references, average vertex normals; retains chunk-local metre coordinates and is for presentation mesh only.
- `CaveLodPlan planCaveSeamSafeLod(vector<CaveLodEntry>)`: validates common physical chunk span (resolution * spacing), unique chunk keys and maximum 64 resolution; sorts result and promotes every face-connected resident group of chunks to the finest sample spacing within that group. Negative XYZ neighbors are supported. Distant disconnected groups retain separate LODs.
- `tests/terraweave_caverns_lod_weld_test.cpp`: duplicate welding, reindexed triangles, degenerate index elimination, deterministic repeatability, 3 connected + 1 disconnected LOD selection, duplicate rejection and bad-index checks.
- Existing `engine/world/TerraWeaveCavernsSeamAudit.h` remains the next validation gate. Do NOT claim a welded mesh is crack-free without testing the generated boundary edges.

## STRATA / ownership
- STRATA is GPU presentation hierarchy / LOD owner; it consumes immutable, versioned visual cave meshes. STRATA must not change Box3D, SpatialWorld or native NavMesh authority. Source-wide cluster seams and normal/material parity still require actual STRATA testing.
- The conservative group-LOD approach limits mixed-resolution cracks by avoiding mixed sampling among directly connected chunks, not by creating true mixed-LOD transition geometry. It may over-refine an extended connected subterranean world and is not a long-term scalability substitute for mixed LOD seams.

## Tests performed
- GitHub source/test/skill commits succeeded.
- **Not executed:** C++ compiler, CMake/CTest, ASAN/UBSAN, same-LOD seam audit on generated neighboring cave meshes, mixed-LOD shader/GPU screenshot, STRATA GeometryScene, Box3D or runtime navigation. Do not claim PASS.
- Suggested first command in a full checkout:
```sh
c++ -std=c++20 -O2 -I. tests/terraweave_caverns_lod_weld_test.cpp -o /tmp/caverns-lod-weld && /tmp/caverns-lod-weld
```
Then register into CMake and run all stages 1–13 with targeted integration and GPU acceptance.

## Limitations and next work
1. True mixed-LOD transition meshing (Transvoxel or a well-defined matching border triangulation) and STRATA cluster seam certification. Until then, conservatively use a single sample spacing per connected active group and require `auditCaveSameLodSeam` before promotion.
2. Verify cross-chunk normals, orientation, material/UV seams and reverse-axis neighbors. Winding and topology of welded meshes are unchanged; non-manifold and distinct-index zero-area faces are not fixed.
3. Implement GPU-fenced per-shard GeometryScene retirement / source-level incremental reuse, rather than full-batch replacement and GPU idle shutdown.
4. Complete the host-controlled cross-render/SpatialWorld/Box3D/NavMesh publication protocol, rollback, and actual cave gameplay and edit/save integration.
5. Continue adding skill instructions and a separate handoff after every implementation stage. No main merge without compilation and acceptance evidence.
