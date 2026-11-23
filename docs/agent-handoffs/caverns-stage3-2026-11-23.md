# TerraWeave Caverns stage 3 — surface entrances (2026-11-23)

## Source / provenance
- Repo: `chromealex/AIVERSE`
- Working branch: `feature/terraweave-caverns-core-20261123` from `main` at branch creation.
- Refactor mesh commit: `ecace7aa329fa256defb3577db0ba332243197c7`
- Entrance implementation commit: `b2c548bcdb8f5bd8c1ea5d72606bbf52cb94b562`
- Test commit: `8a98935a28950a06d6c7bfe40edb169038b07c66`
- Skill update commit: `2fed8a96757189e325c3ba67ab674973c35ac056`
- Current stage handoff is a later commit; check branch HEAD. **Not merged into main.**

## Code changes
- `engine/world/TerraWeaveCavernsMesh.h`: extracted templated `generateCaveMeshSampled` so alternate volume fields can reuse same triangulation. Preserved `generateCaveMesh` public wrapper.
- `engine/world/TerraWeaveCavernsEntrances.h` (new): `EntranceSettings`, `Entrance`, `SurfaceRectCm`, `generateEntrances`, `entranceFieldCm`, `surfaceHoleAt`, `combinedFieldCm`, `generateCaveMeshWithEntrances`.
- `tests/terraweave_caverns_entrances_test.cpp` (new): partition identity on half-open X bounds; cave target point; shaft union field; exclusions; disabled generation; mesh smoke.
- `skills/world/aiverse-terraweave-caverns/SKILL.md`: mandatory new stage-3 API and integration rules.

## Algorithm
- Anchored world-space XZ lattice with signed floor division and hashed seed (deterministic independent of call/partition order).
- Candidate chosen only if chance permits, center not excluded, and vertical descent below sampled terrain finds `caveFieldCmUnchecked>0`. Absent connection: skip entry.
- Shaft union extends from cave hit to just above surface, preserving geometry above old `caves.maxYcm`. Uses approximate radial integer density.
- `surfaceHoleAt` exposes a footprint for the owning terrain adapter; it **does not** modify TerraWeave heightfield, physics or rendered surfaces.
- The entrance set supplied to mesh generation must cover the chunk plus its density-gradient halo to avoid seams. Terrain surface sampler must be a deterministic, versioned immutable snapshot.

## Tests actually performed
- Code and docs committed and re-readable via GitHub connector; full runtime build/CTest **NOT RUN**.
- Standalone compile in this execution environment **NOT RUN** because the container could not reach raw.githubusercontent.com (DNS failure).
- New stage-3 test **NOT EXECUTED**. Do not claim tests passing. Suggested command:
```sh
c++ -std=c++20 -O2 -I. tests/terraweave_caverns_entrances_test.cpp -o /tmp/aiverse-caverns-entrances && /tmp/aiverse-caverns-entrances
```
- Also compile existing stage-1 and stage-2 tests; add CMake/CTest target and cross-chunk / negative-coordinate coverage before merge.

## Important limitations and next steps
1. Need actual terrain mesh footprint cutout, matching surface cave opening (heightfield cannot represent holes by height alone).
2. Entrance connected to one positive-density cave point but navigation clearance/entrance path has not been verified.
3. Current generator is expensive for larger areas: 3D cave field recomputed for each entrance probe, plus mesh field sampling, gradient normals.
4. Smooth mesh is not vertex-welded; varying LOD and seam transitions not yet supported.
5. Add deterministic cached entrance catalogue, terrain material blending, collision/navmesh rebuild, cave shaft entrances and GPU/editor adapters.
6. Run compiler/CTest and correct issues, then integrate opt-in bridge with TerraWeave; do not merge into main until verified.
