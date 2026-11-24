# TerraWeave Caverns — Stage 4, native surface cut geometry (2026-11-23)

## Status / provenance
Repo `chromealex/AIVERSE`, working branch `feature/terraweave-caverns-core-20261123`, based on main at stage-1 branch creation. No merge into main.
- `9880965aca3535dbc6adf7f44290f5afefd435e8`: `engine/world/TerraWeaveCavernsSurfaceCut.h`
- `5ae511567401d051b250b1c6d5ba14aff43300bc`: `tests/terraweave_caverns_surface_cut_test.cpp`
- `f7d694efb553de389db329d815f9ad23aca903b9`: skill update
- Latest branch HEAD also contains this handoff.

## Existing ownership and compatibility researched
Read `engine/world/TerraWeaveTerrainPatch.h` and `engine/render/TerraWeaveTerrainPatchRender.h`.
The canonical `TerrainPatch::collision` supplies both render and physical heightfield triangles. Its publisher explicitly verifies count `(resolution-1)^2*2`, and `makeEarthworksTerrainModel` requires identical fixed count. It is unsafe to publish a clipped variable-count surface through these APIs without extending BOTH contracts. Adaptive visual LOD is presentation only and must not enter authoritative collision.

## Implemented source
- `cutTerrainEntrances(patch, entrances, polygonSides, triangleBudget)` subtracts an inscribed polygon entrance aperture from each native authoritative terrain triangle. Each source triangle's original world-space Y plane is preserved through interpolation of the split vertices. World cm double XYZ output triangles for downstream same-list render/collision conversion.
- Spatial filtering by entrance AABB; deterministic stable id ordering; rejects duplicated ids and invalid configurations; bounded triangle count.
- Output `CutSurface {cell, sourceRevision, triangles, dirtyRegions}` is pure data only. Dirty regions are candidate hints for the publisher to consume AFTER an atomic owner-thread swap; they are not emitted as live ECS/navigation changes.
- `tests/terraweave_caverns_surface_cut_test.cpp` checks native sample patch triangle count, center removed, repeatability, invalid side count.
- Updated `skills/world/aiverse-terraweave-caverns/SKILL.md` with stage-4 API, publisher warning and next integration.

## Tests — actual execution
GitHub create/update operations reported success. The stage-4 source and test were NOT compiled or run against the checked-out engine; no CTest, ASAN, GPU, navmesh or Box3D evidence exists yet. Suggested standalone compile in a full checkout:
```sh
c++ -std=c++20 -O1 -I. tests/terraweave_caverns_surface_cut_test.cpp -o /tmp/aiverse-caverns-cut && /tmp/aiverse-caverns-cut
```
Do not claim full runtime playable caves from this stage.

## Remaining critical integration
1. Introduce a variable-triangle native `TerrainPatch` extension or separate verified publisher that atomically swaps one shared geometry to **both** `render::Model` and `SpatialWorld`, after complete collision-ID and geometric validation. Do NOT bypass the original publisher invariants.
2. Publish dirty rectangles after successful geometry + collision swap only, then rebuild native navmesh with the existing `engine/nav/NavMesh.h` route; invalidate foliage and Horizon coverage accordingly.
3. Generate transition wall/rim from polygon boundary to shaft surface. At present cutter's polygonal hole lies INSIDE circular shaft radius, so a remaining annulus may obstruct access; match shaft cross-section or use sufficient transition geometry.
4. Reconcile graphics/collision settings with same source revision, replay/rollback, streaming and authored exclusions; protect against duplicate entrance ids across adjacent chunks.
5. Test adjacency/LOD and negative coordinates, large triangles, holes touching tile boundaries, overlapping entrances, winding and degenerate triangles.
6. Add CMake/CTest and full engine build, then Metal/GL/Vulkan visual QA and benchmark. Prioritize this verification before any main merge.

The cave generator remains genre-independent and opt-in; PHYSIS, NEREID, Box3D and editor integrations are not implemented here.
