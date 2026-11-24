# TerraWeave Caverns — Stage 2 handoff (2026-11-23)

## Provenance
- Repo `chromealex/AIVERSE`.
- Branch `feature/terraweave-caverns-core-20261123`, based on remote main at branch creation.
- Mesh implementation commit `a0013de642fbdd098583ddb469fb9fc1cd54af5a`; later test, skill and handoff commits follow.
- Not merged to main.

## Implemented
- `engine/world/TerraWeaveCavernsMesh.h`: header-only CPU geometry: `caveFieldCmUnchecked`, validated `caveFieldCm`, `CaveMesh`, `MeshVertex`, and `generateCaveMesh`.
- Integer-centimetre approximate signed field of independently seeded sphere chambers and capsule tunnels; hard containment inside vertical cave band. Positive = void. Not exact Euclidean signed-distance field.
- Marching tetrahedra (six global-consistent tetrahedra per cube), edge interpolation, field-gradient normals and orientation; X/Y/Z 3D lattice and local-metre vertex positions.
- Mesh resolution maximum 64. Same-resolution neighbor vertices sample shared worldwide lattice; no cross-resolution stitching implemented.
- `tests/terraweave_caverns_mesh_test.cpp`: repeatable mesh sample, nonempty triangulation, disable settings, field band check.
- `skills/world/aiverse-terraweave-caverns/SKILL.md`: updated agent API guide.

## Test evidence and limits
- A local C++20 test of the stage-2 mesh algorithm ran, produced **3252 triangles**, passed repeatability/empty volume. That local test used a reconstructed stage-1 dependency header because public GitHub raw download was unavailable in the execution container. This is **not full checked-out repo validation**.
- Published GitHub source has not been compiled in the complete repository and has not undergone CTest, neighboring chunk seam/LOD, ASAN, multi-platform or RHI tests. Those remain NOT RUN.
- `generateCaveMesh` creates duplicated triangle vertices: use mesh welding or index deduplication before shipping.
- Expensive field sampling (27 neighborhood cells plus up to 81 capsule checks per query) and normal samples; use async cache and generation budgets, not continuous per-frame generation.
- Mesh is only a local CPU geometry product, no collision, navigation, editing, water/PHYSIS integration, streaming scene adapter or surface exits.
- Different LOD and material assignment remains unspecified. No guarantee of smoothness beyond tessellation density.

## Next steps
1. Cross-compile published headers using local full checkout and register focused CTest.
2. Verify face topology and vertex equality across negative/positive adjacent chunks, and fix any seams.
3. Implement smooth density + TerraWeave surface height intersection to cut authored entrances.
4. Implement opt-in VoxelGen carve adapter with authored edits winning.
5. Add async chunk cache/LOD, renderer and collision/navmesh scene adapters.
6. Add editor controls and resource limits.
