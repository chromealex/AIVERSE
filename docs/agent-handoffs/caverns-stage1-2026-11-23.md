# TerraWeave Caverns — stage 1 handoff (2026-11-23)

## Branch and provenance
- Repository: `chromealex/AIVERSE`
- Base: remote `main` at branch creation, 2026-11-23
- Working branch: `feature/terraweave-caverns-core-20261123`
- Stage 1 first code commit: `5f0ef753c0326183c390bd4b25b295591ba1dcc6`
- This file is itself committed subsequently; read branch HEAD for the latest commit.
- **Not merged into main**.

## Implemented
- `engine/world/TerraWeaveCaverns.h` is header-only C++20, genre-independent.
- `Settings` bounds: seed, global cell length, tunnel/chamber radii, independent edge/chamber probabilities and half-open vertical range.
- `isVoid(settings, PointCm)`: global integer world-space emptiness query. Negative-coordinate floor division, hashed globally aligned jittered centers, randomly connected positive-axis capsule tunnels, optional spherical chambers. Each point scans the bounded adjacent 3x3x3 lattice; repeated/chunked queries are invariant to generation order.
- `generateChunk(settings, ChunkKey3, resolution, spacingCm)`: dense voxel-style occupancy mask for cell-center world positions, with X-fastest indexing. Not a rendering mesh.
- `tests/terraweave_caverns_test.cpp`: assert-style contract tests for repeatability, direct-sample agreement, negative coordinates, empty settings, invalid configs.
- `skills/world/aiverse-terraweave-caverns/SKILL.md`: agent-facing API and constraints.

## Geometry and ownership
- No new terrain renderer or mutable world state. TerraWeave retains heightfield surface ownership. Existing `libraries/include/aiverselib/systems/voxelgen/VoxelGen.h` keeps block-world generation ownership. Caverns is an optional source of subterranean occupancy only.
- Not yet connected to VoxelGen, density-to-mesh conversion, authored terrain openings, TerraWeave surface, PHYSIS, NEREID, physics, navmesh, editor or streaming runtime.
- Void is sampled at cells, not guaranteed topological connectivity. Random edge choices can create isolated caves.
- Integer capsule projection uses Q16 truncation; generator output compatibility requires a version bump if changed.
- Worst-case roughly 27 chamber plus 81 segment predicates per 3D sample; optimize before large-scale full-resolution generation.

## Tests
- Added C++ test source. **Not compiled or executed in this stage**; no CTest target registered yet.
- No performance, GPU, editor, determinism cross-platform or replay proof claimed.
- Suggested standalone command: `c++ -std=c++20 -O2 -I. tests/terraweave_caverns_test.cpp -o /tmp/aiverse-caverns-test && /tmp/aiverse-caverns-test`
- Integration agent should then register a CMake target, run CTest and cross-platform comparisons.

## Next stage
1. Implement smooth signed-distance density and surface mesh generation with seam-normal rules.
2. Add entrances that respect TerraWeave ground surfaces and authored exclusions.
3. Add optional VoxelGen carve adapter without changing existing voxel behavior unless enabled.
4. Add collision/navmesh hooks, streaming and editor tools.
5. Preserve exact generator settings/version in world save data.
