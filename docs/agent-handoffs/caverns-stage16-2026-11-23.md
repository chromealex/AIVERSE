# TerraWeave Caverns — Stage 16 handoff (2026-11-23)

## Repo and provenance
- Repository: `chromealex/AIVERSE`.
- Branch: `feature/terraweave-caverns-core-20261123`, initially forked from main; **not merged into main**.
- `dea5792f1e9f69cbbd77cdc619dab5d8f178dbc1`: `engine/world/TerraWeaveCavernsSeamGate.h`.
- `e4ea02df423f3629d71b15c194e0670d478bdb32`: change to fail closed for stage-14 subdivided seams until the modified coarse mesh is really staged.
- `3c74437726a8391ced1ea21efb14bbe323639347`: `tests/terraweave_caverns_seam_gate_test.cpp`.
- `d69013547a01edfea5751f3af4852866a4fa642b`: `skills/world/aiverse-terraweave-caverns/SKILL.md` stage-16 instructions.
- This handoff follows those commits; read branch HEAD for final SHA.

## Implementation
- Added pure, genre-independent `CaveSeamPublicationGate` owning a bounded map of immutable, revisioned `CaveCertifiedChunk` data and a per-face certification table. It does NOT mutate renderer or physics directly.
- `upsert` and `erase` invalidate adjacent certificates; `analyze` checks each loaded adjacent face in canonical +X/+Y/+Z order, taking care of both negative and positive directions by evaluating both endpoints.
- If two border edge sets match exactly, the face receives a certificate including the actual render revisions. Otherwise the gate requests regeneration at a common finest spacing for the coarse member. Stage-14 collinear subdivision is also treated as a required regeneration unless its modified mesh has been separately staged: prevents incorrectly showing the old coarse mesh.
- `requests(budget)` exposes bounded stable rebuild requests with serial tokens and source revision; `accept` requires exact key/serial/LOD/source revision; host must then run `analyze` again.
- `canPublish` fails closed if a loaded neighboring face is uncertified, the current render revision mismatches, or a rebuild remains pending.
- Does not change STRATA, GeometryScene, spatial collision, Box3D, NavMesh or authoring. Instead, it is an explicit gate for their later owner-thread publication.
- Test source exercises incompatible neighboring grids, stale result refusal, accepted regenerated pair, required re-analysis and revision invalidation.

## What was actually tested
- GitHub connector commits were successful.
- No full checkout compiler or CMake/CTest, GPU, actual procedural mesh geometry, STRATA model adoption, Box3D/NavMesh, sanitizer or runtime acceptance executed on this stage. Test source authored only; status **NOT RUN**.
- Suggested C++20 focused test in full checkout:
  `c++ -std=c++20 -O2 -I. tests/terraweave_caverns_seam_gate_test.cpp -o /tmp/aiverse-caverns-seam-gate && /tmp/aiverse-caverns-seam-gate`
  followed by all prior Caverns tests and actual GeometryScene integration.

## Known constraints / risks
1. If equally spaced neighboring meshes have different border topology, no coarser side exists to upgrade. The gate intentionally remains blocked until the field/mesh producer is fixed. An infinite retry would be incorrect.
2. `analyze` re-evaluates all loaded pairs and copies inputs to `resolveCaveFaceLod`; optimize and cache face fingerprints before large worlds.
3. `CaveRebuildRequest` carries one source revision, so host must also ensure a globally consistent generator/terrain/entrance snapshot. Revisions alone are not proof of same configuration.
4. The gate tests actual loaded adjacent pairs only, not hypothetical future streaming neighbors. The owner must re-run certification on adjacent load/unload, edit or LOD change.
5. Neither shard streaming nor this gate performs an atomic multi-system render/physics commit. Current `CutTerrainResident` and `CavernsBox3DPublisher` still have separate immediate publish paths; no global rollback guarantee.
6. STRATA remains presentation LOD owner; its independent internal simplification must preserve source boundary certification. No Metal/Vulkan/GLES raster proof exists for these generated caves.

## Next actionable steps
1. Wire gate to `CaveShardRegistry` and worker scheduling, with immutable mesh/config snapshots; rebuild and re-audit exactly the affected neighboring chunks before GeometryScene exposure.
2. Add actual staged owner-barrier publication with GPU fence-aware retirements and reversible Box3D/SpatialWorld transitions.
3. Fix the current test/build backlog: register stages 1–16 in CMake, compile and run before main merge.
4. Extend editor painting, native save/replay and cave decoration after structural foundation.
5. Continue creating a revised skill and independent dated handoff after every implementation stage.
