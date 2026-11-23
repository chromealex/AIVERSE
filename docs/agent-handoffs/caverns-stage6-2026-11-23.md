# TerraWeave Caverns — Stage 6 handoff (2026-11-23)

## Provenance
Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123` (feature branch; not merged into main).
- `90cc96c220fa75b6bfbd18289a0e65cacf11089c` — Box3D adapter
- `5035c8331bb8a7370afb02c530ad115535e5dd6b` — native NavMesh bridge
- `a8e4c59e81dd9aaa4142e159f080368056cd1cbf` — STRATA compatibility/lease contract
- `9028aff9889b284fd161d47581e6a06c0daa3885` — NavMesh and STRATA unit contract test
- `78bc71bd21652cc4623948e596df52d4556361ed` — Box3D payload test
- `e19f4ed6a5f03780f15d086a97a6b51a36e65cd2` — skill update
- This handoff commit is subsequent; see branch HEAD.

## Ownership and references inspected
- Existing `engine/world/TerraWeaveBox3DTerrain.h` and `engine/physics/Scene.h` own Box3D scene body lifecycle. New optional Caverns publisher uses their compatible body creation/publish pattern without overwriting standard terrain.
- Existing `engine/world/TerraWeaveNavMeshBridge.h` and `engine/nav/NavMesh.h` own the native, genre-independent navmesh.
- STRATA is **virtualized rendering/LOD geometry**, not a geological strata simulation: read `docs/agents/strata.md` and `docs/agent-handoffs/strata-virtual-geometry.md`. It owns cluster hierarchies, rendering-side residency and presentation LOD; TerraWeave retains cave world generation, geometry versions and changed chunks.

## Added API
- `engine/world/TerraWeaveCavernsBox3D.h`:
  - `makeCavernsBox3DBody(CutNativePayload,tileSizeMetres,userData)` deduplicates native collision vertices and builds a static triangle mesh without an independently triangulated surface.
  - `CavernsBox3DPublisher::publish/unload/shutdown` uses generation-safe scene handles; creates before removing old body, skips stale revisions.
- `engine/world/TerraWeaveCavernsNav.h`:
  - `surfaceEntranceBlocked(config, entrances, previous)` immutable capture for existing EarthworksNavSource; blocks surface columns intersecting cave openings, in addition to any previous blockers.
  - `refreshCavernsSurfaceNav(mesh,source,dirty,error)` forwards dirty rectangles to existing navmesh refresh.
- `engine/render/TerraWeaveCavernsStrata.h`:
  - `CaveGeometryLease`, `planCaveStrataLease`, `caveStrataLeaseCurrent`: pure revision metadata and conservative eligibility gate. STRATA mesh adoption remains owned by GeometryScene; no new GPU/LOD implementation or assertion of actual STRATA rendering.
- Unit test sources: `tests/terraweave_caverns_stage6_test.cpp`, `tests/terraweave_caverns_box3d_test.cpp`.
- Skill updated: `skills/world/aiverse-terraweave-caverns/SKILL.md`.

## Validation status
- GitHub source writes reported success.
- **No local C++ compiler, CMake/CTest, Box3D live scene, navmesh refresh, GPU, STRATA selection, or platform acceptance ran in this stage.** Tests are authored only.
- Suggested commands in full engine checkout: `c++ -std=c++20 -I. -O1 -c tests/terraweave_caverns_stage6_test.cpp -o /tmp/cave-stage6.o` and `c++ -std=c++20 -I. -O1 -c tests/terraweave_caverns_box3d_test.cpp -o /tmp/cave-box3d.o` (Box3D include directories may need CMake target configuration). Then proper linked native tests + CTest.
- Do not claim real-time cave ingress and physics/navmesh parity or active STRATA rendering from these isolated APIs.

## Open integration tasks
1. Runtime host coordinator must publish the same cave-cut topology to Renderer3D, SpatialWorld, and Box3D, hide original tile geometry/collision, reconcile revision order, then refresh NavMesh and vegetation. Guard partial failure across physics and graphics owners.
2. Current surface NavMesh bridge removes surface spans at openings but does **not** generate multi-level cave navigation or passage links. Design a multi-span nav::Source from true cave floors with agent radius/height clearance.
3. STRATA: feed completed immutable cave geometry source snapshots through existing GeometryScene at an owner-thread barrier, preserve Renderer3D fallback until exclusive draw routing is verified; refresh cluster assets on cave edit/revision change. Verify cross-chunk seams and multi-LOD crack handling.
4. Add shaft-wall/rim transition geometry. The cut polygon is inscribed in the cylindrical shaft; residual annulus may block a character.
5. Add bounded cave mesh cache, chunk streaming residency, authored cave editor overlays/brushes, saves/replay versioning, CMake tests, sanitizer and GPU acceptance.
6. STRATA must not be a second collision source; retain immutable authoritative surface triangle list independently of GPU LOD selection.
