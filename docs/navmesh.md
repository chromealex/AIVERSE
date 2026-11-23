# Navigation mesh (`engine/nav`)

A genre-independent navigation mesh: one header, `engine/nav/NavMesh.h`, Fixed / integer math only, no RTS, FPS or
other gameplay types. Modules describe walkability; the mesh does the rest.

| user | source of walkability | entry point |
|---|---|---|
| RTS units (`modules/rts`) | passable cells of a path layer, links = `canStep` both ways (cliffs, ramps, slopes) | `MapGrid::findPath` with `movement.pathfinding: "navmesh"` (docs/pathfinding.md) |
| FPS bots (`libraries/.../bots/NavMesh.h`) | supported floor spans under a clear body from AABB collision, links = motor step + body sweep | `buildNavMesh`, `navPath`, `navStep` (docs/bots.md) |
| anything else | implement `nav::Source` | `nav::Mesh::build`, `findPath` |

## Model

- **Columns and spans.** The world is a raster of `width x depth` columns (`Config::cell` world units, corner at
  `originX/originZ`). `Source::spans(x, z)` lists the walkable surfaces of a column (ascending height, stacked floors
  allowed, an `area` class each). `Source::link(x0, z0, i, x1, z1, j)` says whether an agent crosses between span `i`
  and span `j` of an orthogonal neighbour; it must be symmetric.
- **Rectangles.** Linked spans of one area (and one height with `flat`) merge greedily (+x, then whole rows along +z)
  into rectangles. Rectangles never cross `tile` borders (default 32), so a change rebuilds only its tiles.
  Every point of a rectangle is walkable and every straight line inside it is too.
- **Portals.** For every side of a rectangle: runs of linked crossings to one neighbour rectangle. Each end records
  whether a wall touches that corner (`solid`) and on which side the wall continues (`pushLo/pushHi`).
- **Tile classes.** With `Config::maxRectRatio` > 0 a tile whose merge gives more than that many rectangles per span
  is *coarse* (a maze: rectangles shrink to cells and buy nothing): it keeps no rectangles, `coarseAt(x, z)` reports
  it and callers route through it with their own solver (RTS hybrid: grid A*). The class is a pure function of the
  tile, so it changes with the tile on refresh. `tileInfo(i)` gives spans / merged rectangles / class per tile.
- **Ids.** Rectangle ids are dense in tile order and stay valid until a refresh rebuilds tiles; `digest()` hashes the
  content (tests compare incremental refreshes with fresh builds).

## Queries

`findPath(from, to, Query, out, corridor)` returns `from, corners..., to`:

1. `locate` both points (column, then the span nearest in height within `yTolerance`).
2. A* over rectangles; the cost to enter a neighbour is the distance to the portal's crossing point (where the straight
   line to the goal meets the portal, clamped to it), scaled by `areaCost[area]`; heuristic = straight distance.
   `maxVisits` bounds the rectangles expanded. Ties by rectangle id.
3. Funnel (string pulling) over the portals of the corridor; portals shrink by `margin` at wall corners (never past
   their middle). Corners that would run along a portal line over a wall stretch step off it by up to half a cell,
   checked exactly against the corridor rectangles.

False: a point off the mesh, no connection, or the budget ran out. Callers keep their own fallback (RTS: grid A*).

`Query::bestCorridor = true` replaces step 2 with A* over portal points (both shrunk ends and the middle of every
portal): it picks the corridor more reliably (paths a few % shorter than the default search and than string-pulled
grid paths) at about grid speed in open areas, but it is slow in mazes (many tiny portals), so it is off by default.

## Updates

`markDirty(x0, z0, x1, z1)` marks the columns whose spans or links may have changed (include the ring of columns whose
links to them change); `refresh(source)` rebuilds the dirty tiles and the portals of their 3x3 tile neighbourhood.
`locate` / `findPath` refuse a dirty mesh, so callers refresh first (RTS does it in `MapGrid::navFor`).

## Determinism and limits

- Fixed math, fixed iteration order, ties by ids; results are a pure function of the source (refresh == build).
  Meshes are derived data: never hash or save them, rebuild from the same source.
- Rectangular cells only: no slopes inside a span, no triangle-collision import, no off-mesh links (jumps, ladders),
  no streaming between separately built regions. Corner clearance is a margin, not an erosion of the walkable area:
  narrow passages stay open and a path may graze a wall corner it does not turn at.
- `maxSpans` (default 2^24) fails a build explicitly instead of truncating it.
- Measured on a VM with random obstacles: 256x256 build 4 ms, 1024x1024 build 86 ms, refresh after a 3x3 change 0.3 ms,
  query 0.05 ms (256²) / 0.7 ms (1024²). Unreachable goals cost a search of the whole connected region (no component
  labels: they would make every refresh global).

## Which solver where (measured)

`aiverse_headless --project DIR --map maps/X.json --bench-nav N [--bench-clear C] [--bench-ratio R]` runs the same N
random ground queries with `grid`, `navmesh` and `hybrid` (time for found / unreachable goals, waypoints, length) and,
per 16x16 tile, times paths inside the tile with grid A* and with the rectangles, bucketed by rectangles per cell.
VM, 2026-11 (numbers are wall clock, diagnostics only):

| map | grid (A*/HPA* + string pulling) | navmesh | hybrid |
|---|---|---|---|
| template maps 48x36..64x48 | 0.006-0.05 ms | 2-4x faster; length -1..+4% | about grid; fewer waypoints |
| mixed 256² (plain, city, caves, maze) | 0.30 ms | 0.21 ms, -1.7% length | 0.24 ms (-19%) |
| mixed 1024² | 3.2 ms | 5.7 ms | 3.3 ms |
| heightfield 1024² (`mapgen.py --big`) | 1.06 ms | 15 ms (slopes cut the map into thousands of rectangles) | 1.22 ms, -1% length, -38% waypoints |

Per tile: rectangles answer 3x faster than grid A* below ~0.10 rectangles per cell; from ~0.20 grid A* is 2-7x
faster (mazes, caves). Hence `movement.hybrid.mazeRatio` defaults to 0.15. Summary: `navmesh` for small and medium
open maps; `hybrid` for big or mixed maps (it never falls far behind the grid and keeps the navmesh where it wins);
`grid` stays the default.

Tests: `navmesh_core_*`, `navmesh_rts_*` in `tests/tests_navmesh.inc`; FPS: `aiverse_library_navmesh_tests`.


## Building vertical circulation adapter (Stage 14)

The existing nav::Mesh has no integrated off-mesh portals. Instead,
engine/buildings/BuildingNavMeshBridge.h resolves building StructureGraph
stairs/lift edges onto actual current nav polygon IDs and uses native
findPath for horizontal room legs. Stair actions include physical step
waypoints; lift edges need explicit asynchronous boarding/ride actions.
Links become stale after NavMesh::refresh, and dynamic platform collider
changes mark dirty native mesh tiles. See docs/building-runtime-circulation.md.
