# TerraWeave Stage 50 — real incremental engine navmesh from Earthworks

Date: 2026-11-19. Branch: `feature/terraweave`. Target PR #24 remains Draft.

## Code commits

- `53e048f216d89b7f2bfa95169dcf9c1ab6339873`: NEW `engine/world/TerraWeaveNavMeshBridge.h`. `EarthworksNavSource` implements the existing genre-independent `nav::Source` based on the SAME `EarthworksLayer::sampleHeightCm` used by render and collision. It provides one walkable Fixed-height span per nonblocked navigation column and links orthogonal neighbours only when deterministic step-height/slope limits permit. Optional blocked callback accepts immutable building/road/water geometry: never query live mutable NEREID ECS from a nav worker. `earthworksNavMeshConfig` returns native `nav::Config` with slope-compatible `flat=false`.
- `20581e1d7f3389e9cb90167de087583cc44b8194`: limit source away from Earthworks world boundaries, include needed standard headers and refuse refreshing a mesh that has never been built.
- `05dda2f6603a3ec7c321c2777eb3e30677f21661`: `TerrainMutationCoordinator::refreshNavMesh(nav::Mesh&,EarthworksNavSource&,string*)` consumes the currently published resident tile dirty rectangles as **input**, marks dirty navmesh columns with a one-cell halo, and invokes the existing `nav::Mesh::refresh` incremental tile/portal rebuild. Does NOT consume the terrain notification queue: Horizon/foliage and other navmesh listeners must see the same revision using `takeTerrainDirty`.
- `c26b29f40b6539952c97b108df5b891e055a302a`: remove an unimplemented navmesh water toggle. Water/wetness is authoritative through an optional host-blocked callback, not inferred from an elevation heuristic.

## Source and geometry contract

`EarthworksNavConfig` is bounded to <=262144 columns, signed world-centimetre origin and 25..3200cm column widths, valid slope/step thresholds and configured native tile size. A source provides `spans(x,z)` using `Fixed::fromRatio(heightCm,100)` and `link(x,z,...)` using integer delta/grade tests. `markEarthworksNavDirty(mesh,source,worldRectCm)` converts half-open world cm rect to inclusive navmesh columns with a one-column neighbour halo; `refreshEarthworksNav` delegates portal/rectangle rebuild to existing `engine/nav/NavMesh.h`. This is a SINGLE genre-independent NavMesh (not a second RTS/FPS pathfinder). The host must first call `mesh.build(source,earthworksNavMeshConfig(cfg))` and must invalidate water-blocked columns when NEREID water geometry changes independently of soil.

**Runtime lifecycle**: `TerrainMutationCoordinator::excavate` / `erode` mutate Earthworks and sparse vegetation state at authoritative game tick. `publishVisible` publishes matching render::Model and SpatialWorld triangles at the graphics/collision owner barrier; `refreshNavMesh` can then rebuild native pathfinding dirty tiles from those same committed heights. The caller still must decide when simulation can enter each tile and avoid race conditions between physics/navmesh and GPU owners. On a `refresh` failure, `nav::Mesh` stays dirty and path queries refuse to use obsolete connections. Do not call `takeTerrainDirty` until all interested consumers have seen the change.

## Unfinished / verification

This is functional native header/API integration, not an automatically installed FPS or RTS client. Dynamic Box3D terrain and falling plant adapters exist in Stage49 but are also opt-in. `nav::Source` currently supports one sampled walkable height per nav column; volumetric caves, dynamic overhangs, multi-floor walkability and water passage require project-specific additional spans. There is no automatic harvesting obstacle/navmesh bridge for sleeping fallen-tree physics bodies yet.

Actual verification: GitHub source commits and readbacks only. NO new CMake/Clang/GCC build, CTest, real navmesh route regression, FPS/RTS/Box3D scenario, Metal/GL screenshot, benchmark or cross-platform QA performed on Stage 47–50 changes. Prior 9/9 TerraWeave CPU and 1/1 GPU timer tests are older head results only.

Next: wire an opt-in native WorldScene/authoritative game integration with the project's existing pinned chunks and NEREID ECS water providers (without changing genre-independent ownership); add authoritative Box3D fall obstacle and save/replication bridge; then run dedicated QA and fix first actual failures. Keep PR Draft.
