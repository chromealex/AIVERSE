# TerraWeave Caverns — Stage 7 handoff (2026-11-23)

## Provenance
Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123` created from main at stage 1. Not merged into main. Stage 7 commits:
- `d41a13d94360fca86d608ad3691a7f887797ea26` streaming planner
- `e09514232ce90189974fd4db6cd2eb9c3523a380` multi-span native cave navigation source
- `63b646ac738baf43c85fda7c470412fd8f617a59` shaft wall mesh
- `cb578683b014b286c0a1ed8f6ac6bd311d9ce4d4` globally unique stream invalidation generations
- `b4812f2c7cb33cffb4b98d659c57377ab8c7a743` stream/shaft regression source
- `0957a532e43be09c5ce937b3a6acb0c02f846974` nav regression source
- `3cc46eb63090a2761dba57ba0ff1592a4bd4ab89` expanded agent skill

## New components and API
1. `engine/world/TerraWeaveCavernsStreaming.h`: bounded `CaveStreamPlanner` for interested chunk keys, deterministic nearest-first Build requests, bounded Retire requests, source revision/visual LOD invalidation, and cancellation of stale job results. Pure planning, no GPU or ECS.
2. `engine/world/TerraWeaveCavernsMultiNav.h`: `CaveNavSource` implements the EXISTING `nav::Source` (multi-span columns, sampled underground floors, clearance at agent height and step/slope links). `caveNavMeshConfig` produces native `nav::Config`. No second A* pathfinder, no FPS/RTS includes.
3. `engine/world/TerraWeaveCavernsShaftMesh.h`: `makeEntranceShaftSurface` emits world-centimetre top/bottom wall rings around a surface entrance, using deterministic angular segmentation. Geometry only; no physical collider or GPU upload.
4. `tests/terraweave_caverns_streaming_test.cpp`: stream budgets, stale completion rejection, shaft geometry counts.
5. `tests/terraweave_caverns_multinav_test.cpp`: nav span ordering and config.
6. `skills/world/aiverse-terraweave-caverns/SKILL.md` documents all stage-7 APIs and boundaries.

## STRATA (explicit user requirement)
- Reviewed `docs/agents/strata.md` and `docs/agent-handoffs/strata-virtual-geometry.md` from main. STRATA owns geometry clusters, LOD selection, GPU residency and presentation. Cave source geometry and revisions remain TerraWeave Caverns' responsibility; Box3D/SpatialWorld authoritative collision and native `nav::Mesh` must not use Strata-simplified visual LOD.
- Stream planner manages CPU work requests only. Stage-6 `planCaveStrataLease` provides eligibility metadata, NOT actual integration with `GeometryScene`. A later agent must wire GeometryScene source adoption and retired resource leases explicitly and prove exactly-once draw ownership with Renderer3D fallback.
- No STRATA APIs, shaders, or global render routing changed.

## Tests and current limitations
- GitHub connector reported successful source/skill commits.
- **No CMake/CTest or C++ compiler, no GPU, Box3D live scene, actual NavMesh pathfinding, GeometryScene STRATA adoption or cross-backend runtime tests were executed for this stage.** All added tests currently NOT RUN.
- CaveNavSource scans the full vertical range for every spans/link invocation. Without caching, navmesh build can be expensive. Floor samples use coarse integer step and only centerline head clearance; they do not guarantee agent radius, vertical shaft traversal or actual walkability near a sloping wall.
- Streaming planner `accept` marks a job ready before a host publishes its resources. In production separate worker completion from successful GPU/physics/STRATA publication and handle failed builds/retries with bounded backoff; also ensure dropping a center does not prematurely free GPU resources in flight.
- Shaft rim height from a general height callback may not equal exact height of the already-triangulated TerraWeave patch. Before production use interpolation of the ORIGINAL authoritative collision triangles at each ring point and align cut polygon/rim winding.
- Terrain / hole geometry remains inconsistent if host leaves original heightfield collision visible; original tile must be retired/hidden atomically across render/SpatialWorld/Box3D before players can enter.
- Need extend CMake test registration and execute actual build, especially C++ include/type integration, after all selected functionality is in place.

## Next concrete stage
1. Create host-side atomic publish coordinator: stage render, SpatialWorld and Box3D replacements; on failure keep old scene and do not emit NavMesh dirty updates. Keep a unique collider-ID lease allocator.
2. Hook active cave chunks and revision changes into GeometryScene's real STRATA source/residency API; keep Renderer3D fallback until verified parity and lease lifetime.
3. Build cached/nav-budgeted multi-span cave surfaces and shaft links, then verify paths and character clearance in actual scene.
4. Weld smooth meshes, implement LOD seam transitions and test adjacent chunks at different resolutions.
5. Author cave editor tools, authored persistence/replay, decoration geology, underwater areas and in-game snapshots.
