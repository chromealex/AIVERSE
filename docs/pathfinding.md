# Pathfinding — A*, HPA*, navigation mesh, flow fields

All pathing is deterministic integer code in `modules/rts` (no floats, ties by cell index / entity index). Costs are
10 per straight and 14 per diagonal step; a diagonal is legal only when both orthogonal detours are (no corner cutting);
passability is per path layer (`UnitType::pathLayer` = move layer + slope class on heightfields); bodies larger than
half a cell pay a clearance penalty in tight cells (A* only). Flat maps and planets (cube-sphere faces stacked in one
grid, `Surface.h`) share the same rules: on planets the 8 neighbours come from the surface's neighbour table, so
face seams and cube corners (7 neighbours) are ordinary steps.

| what | where | used for |
|---|---|---|
| A* | `MapGrid::astarCells` (flat), `astarSphere` (planets) | every short path; long paths on small maps; refinement of HPA* routes |
| HPA* | `MapHpa.cpp` (flat, maps >= 256x256), `MapHpaSphere.cpp` (planet bodies >= 65,536 cells) | one unit's long trip: entrance graph of 16x16 clusters, then A* between consecutive waypoints |
| flow fields | `FlowField.cpp` | many units to one goal: group orders >= `movement.flowField.minGroup`, map / lane waves with `waves: true` |
| navigation mesh | `engine/nav/NavMesh.h` (genre-independent), `MapGrid::navPath` | opt-in `movement.pathfinding: "navmesh"`: one unit's path on flat maps, any angle (docs/navmesh.md) |
| hybrid | `MapGrid::hybridPath` (HPA* graph + navmesh / grid per cluster) | opt-in `movement.pathfinding: "hybrid"`: big or mixed flat maps; flow fields on the same cluster graph |

`MapGrid::findPath` picks A* or HPA* (string pulling on top). Flow fields are opt-in per game or map
(docs/game-format.md "Movement and bodies"); without `movement.flowField` nothing changes.

## Navigation mesh (`movement.pathfinding: "navmesh"`)

`MapGrid::findPath` first asks the layer's navigation mesh (flat maps, goal passable, not a surround-allies search):

- **Source.** `GridNavSource` in `MapGrid.cpp`: a cell is a span when `passable(x, y, layer)`; two orthogonal cells link
  when `canStep` allows both directions (cliff levels, ramps, slope classes). The mesh itself (`engine/nav`) knows no
  RTS types - the FPS bots feed the same mesh from AABB collision.
- **Mesh.** Linked cells merge greedily into rectangles inside 32x32 tiles; portals between rectangles mark wall
  corners. One mesh per path layer, built at match start (`prepareNavMesh`), derived like HPA* (never hashed).
- **Updates.** `block` / `cellChanged` mark the touched tiles (plus a one-cell ring) dirty; the next query rebuilds only
  those tiles and the portals around them. The result equals a fresh build (tested), so saves (`invalidatePathing`)
  and incremental play agree.
- **Search.** A* over rectangles (crossing point of each portal = where the straight line to the goal meets it,
  clamped), then a funnel; corners keep `0.5 * (1 + clearance)` from wall corners along portals and step off walls
  that run along a portal line. `maxNodes` bounds the rectangles expanded.
- **Fallback.** Start or goal off the mesh, no connection within the budget, `soft` paths: the grid search runs as
  before, so unreachable goals still end at the closest reachable cell. `MapGrid::navStats` counts answers / fallbacks.
- **Measured** (`engine/nav` alone, VM, random obstacles): 256x256 build 4 ms, 1024x1024 build 86 ms, rebuild after a
  3x3 structure 0.3 ms, path query 0.05 ms (256²) / 0.7 ms (1024²); paths ~7% shorter than octile grid paths and about
  as long as the string-pulled grid paths. Mode comparison on real and synthetic maps: docs/navmesh.md.

## Hybrid (`movement.pathfinding: "hybrid"`)

One graph, a solver per area:

- **Graph.** The HPA* clusters (16x16) with their border entrances and integer intra-cluster costs are the portal graph
  of the whole map - at any map size, not only from 256x256. Long routes are searched on it with exact grid costs, so
  costs of different solvers never mix.
- **Solver per cluster.** The navmesh uses tiles = clusters with `maxRectRatio = movement.hybrid.mazeRatio` (default
  0.15): open clusters keep rectangles, maze clusters get none and are left to grid A*. Classes are a pure function of
  the cluster's cells and change with them (structures, digging).
- **Trips.** Within 2 clusters: the navmesh when both ends are in open clusters, else the grid search. Longer: the HPA*
  route (`hpaRoute`), each leg refined by its cluster's solver (navmesh corners or A* cells), the joined polyline
  string-pulled with the grid line test (look-ahead 48). No route on the graph = unreachable: the grid search does not
  repeat HPA*, it only finds the closest reachable cell.
- **Flow fields.** `flowHierFor` is true on every hybrid map (`flatHierarchy`): fields use the same cluster graph
  (hierarchical values, clusters computed lazily) instead of whole-map Dijkstra on small maps.
- **Keys.** `movement.hybrid.mazeRatio` (rules or map, 0.01..1). `--bench-nav` / `--bench-ratio` measure a map.

## Flow fields

- **Key and sharing.** One `FlowField` per (goal cell, goal-area radius in cells, path layer, clearance class); every unit of
  the order follows it. The goal area (all passable cells within the radius: `0.3 + spread * sqrt(units)` world units)
  is what spreads a crowd over the target instead of piling it onto one cell.
- **Integration values** = integer cost from a cell to the goal area under exactly the A* step rules (a backward
  Dijkstra: step v -> u legal iff A* could take it). Units follow the steepest strictly-decreasing neighbour, so a
  descent always ends in the goal area and never cuts a corner.
- **Small maps / bodies:** one Dijkstra over the whole grid (with the A* clearance penalty), lazily cached directions.
- **HPA* maps / bodies (hierarchical):** a resumable reverse Dijkstra over the HPA* entrance graph (the same graph and
  intra-cluster costs as HPA*, flat or planet) gives each entrance its cost D to the goal area; a cluster's values are
  a Dijkstra inside the cluster seeded with its entrances' D (and the goal cells it holds). Only clusters that units
  stand in or step into are computed, and the abstract search runs only as far as those need. Values are never below
  the A* optimum and, snapping to entrances like HPA*, a few % above it on average.
- **Invalidation.** `MapGrid::block` / `cellChanged` bump a grid stamp and the changed cluster's stamp; a field
  revalidates on its next use: seeds are recomputed, the abstract D restarts, a cluster block is rebuilt only when its
  cells changed or its seeds (entrance D) differ. Incremental results equal a full rebuild (tested).
- **Cache.** Fields are derived data (never hashed: they are pure functions of the grid). Units following a field
  reference it each tick; unreferenced fields are evicted LRU beyond `maxFields` / `budgetMB`.
- **Steering.** Every 10 ticks (staggered by entity index) and when its waypoint is reached, a unit samples the field:
  the farthest cell of the descent it can see in a straight walkable line (doubling + bisection), or the goal itself
  when close and visible; ORCA avoidance, separation and the yield rule work as for any path. A unit stuck on a field
  re-samples it first (crowds clear by themselves) and falls back to its own A* path after repeated stalls; one pushed
  into a cell that can't reach the goal falls back at once. Planet followers slide along walls on the face axes.

### Crowds (flow-field groups)

- **Arrival contagion.** A follower settles (stops, `Mobile::flowArrived` = goal cell) when it reaches the goal point,
  or when it touches a unit that already settled at the same goal while inside the settled disc
  (1.3 x 0.53 x body diameter x sqrt(settled) + 2). A follower that is stuck or has circled the crowd for 20 s settles
  wherever it touches it (up to twice the goal area); the first follower hovering 2 s in the goal area settles when the
  goal point itself is taken. The crowd fills the area around the goal from the inside out instead of jamming.
- **Pass-through.** Followers of the same field add no velocity obstacles for each other (they move the same way;
  separation keeps bodies apart) and are dropped from the ORCA candidate list before sorting; settled units are static
  obstacles for followers still arriving and never take yield requests (others go around or settle against them).
- **Cheaper avoidance:** separation and ORCA neighbour candidates come from a one-cell grid over the agents with a
  partial sort (agent-mech, bit-identical to the spatial hash); flow followers additionally drop their own crowd from
  the candidates before sorting.
- **Blocked goals** (`movement.blockedGoals: "nearest"`, rules or map; off by default because it picks different cells
  than the classic search): a path to a goal inside a structure / obstacle aims at the free cell around it nearest to
  the start, instead of A* exploring the whole reachable area for the closest cell (chasing a building).

### Measured (VM, headless `--bench-group N --bench-flow on|off`, one move order across the map; `AIVERSE_BENCH_HIST=1`
prints where the units ended up)

See the table in the agent-flowfield report / `docs/planets.md`. On flat 256² / 1024² and a faceSize-128 planet the
order tick drops from hundreds or thousands of ms (one search per unit) to a few ms; total path time falls by 2-40x;
the remaining tick cost is local avoidance in the crowd, not pathing.

## TerraWeave Caverns: shared navigation inputs (Stage41, 2026-11-24)

The cave SDF is an **authoritative geometry source**, not another pathfinding
algorithm. `CaveNavigationDataProvider` in
`engine/world/TerraWeaveCavernsNavigationData.h` evaluates deterministic
clearance, floor support, flooded state, area cost and directed transitions
from one published immutable cave snapshot. The agent profile selects ground,
flight, swimming, climbing or custom policies, with external same-epoch
NEREID/PHYSIS/ECS callbacks where required.

`CaveNavigationGridView` provides a bounded integer 3D graph (signed X/Y/Z,
stable node IDs, six face neighbors and 3D HPA cluster portals). This graph
can feed *existing* A*, HPA*, Flow Fields or Hybrid solvers through an engine
adapter, or an independently registered custom solver. The goal is reuse:
Caverns itself never contains a second A*/Dijkstra/flow-field implementation.

`CaveSharedNavSource` directly exposes `nav::Source`'s multilevel spans and
links for the existing `nav::Mesh` algorithm, from the SAME shared data
provider. Free-flying or 3D-swimming agents must use a true 3D graph; a
2.5D ground NavMesh should not silently represent free flight.

`CaveNavigationPublication` announces revisioned half-open dirty SDF bounds
*after* the real all-owner game commit. It has explicit consumer IDs for
A*, HPA*, Flow Field, NavMesh, Hybrid and Custom. Stream-in/out notifications
also invalidate derived graph topology at an unchanged SDF revision.
`CaveNavigationInvalidation.h` converts those events into 3D grid ranges,
HPA cluster ranges and the existing `nav::Mesh::markDirty` columns.
`CavePathRouter` calls a registered engine/custom callback and rejects
queries against the wrong SDF revision or pending invalidations.

**Integration boundary:** the current `game::MapGrid::findPath`,
`hybridPath`, `flowAcquire`, and HPA* implementations are bound to
`MapGrid`'s 2D path-layer cells. Stage41 provides the 3D graph and
registration API but does not yet refactor those exact implementations to
accept a generic cave 3D graph. They should be adapted via shared engine
pathfinding interfaces, not copied into Caverns, and only then described as
fully native-wired. This feature has not been compiled or runtime-tested yet
under the feature-first workflow; see
`docs/agent-handoffs/caverns-stage41-2026-11-24.md`.

## Shared graph solvers for 3D Caverns (Stage42, 2026-11-24)

The engine now has `engine/nav/GraphSearch.h` (`nav::findGraphAStar`,
`nav::findGraphDijkstra`, `nav::buildGraphFlow`) and
`engine/nav/GraphHierarchy.h` (`nav::findGraphHierarchical`).
They operate over a small common graph protocol
`size() / passable(node) / neighbors(node,vector<nav::GraphEdge>&)`, integer
costs, fixed node IDs, deterministic tie-breaking and bounded visits. All
generic algorithms are in `engine/nav`; no copies of A*/HPA*/Flow algorithms
reside under `TerraWeaveCaverns*`.

`TerraWeaveCavernsNavigationGraph.h` adapts the cave's true 3D ±X/±Y/±Z
adjacency to `nav::GraphEdge`. `TerraWeaveCavernsGraphSolvers.h` constructs a
bounded 3D query region from start/goal and agent profile, supports named
`astar`, `hpa`, `flow`, `hybrid` backends, and returns versioned 3D cm
waypoints. HPA uses a shared hierarchy abstraction, then fine-graph search,
with a global fallback; Hybrid picks that route or direct A* according to
graph size. The flow backend traces the reverse directed integration field.
`CaveGameplayRuntimeFrame` registers these four algorithms by default,
supports configured graph budgets, and still accepts arbitrary named custom
C++ solver callbacks.

`TerraWeaveCavernsNativeNavSolver.h` binds the existing
`engine/nav/NavMesh.h::Mesh` to the common `CaveSharedNavSource`.
Call `registerNativeNavMeshSolver(config)` on the owning runtime; it is
ground/climb multilevel navigation, not a true 3D free-flight volume solver.
The current adapter builds a native mesh on-demand per query rather than
caching tile refreshes, so it is correct API integration but not a proven
high-throughput production NavMesh cache.

`engine/nav/CaveEcsRouteFollow.h` adds a real shared ECS
`CaveRouteFollower` component, `assignCaveRoute(world,entity,route,...)`
and `updateCaveRouteFollowers(world,publishedProvider,physicsCallback)`.
All interpolation uses integer cm and `Fixed` transforms. An out-of-date
SDF revision marks `needsRepath`; without a real same-epoch Box3D/
SpatialWorld collision callback the follower refuses motion. The actual
simulation authority must register this update in its fixed-tick system
schedule and include authoritative route components in state hashing,
serialization and replay as needed. The visual-only GameClient/Editor Play
and lockstep relay server do not control authoritative physics movement.

**Legacy distinction:** `MapGrid::astarCells`, `MapHpa.cpp`,
`FlowField.cpp` and `MapGrid::hybridPath` remain 2D specialized engine
implementations. They have not been rewritten/migrated to use the new
generic 3D graph protocol; the common `engine/nav` graph solvers are newly
shared reusable implementations and do not silently change established
2D gameplay. A future opt-in migration can route MapGrid through common
interfaces after behavior-equivalence QA.

Stage42 is a source implementation awaiting native compilation and
acceptance; no CMake/CTest or gameplay physics tests have been run under
the user's features-before-tests instruction. See
`docs/agent-handoffs/caverns-stage42-2026-11-24.md`.

## Caverns Stage43: authoritative ECS route lifecycle and environment epochs

An actual game or simulated authority can opt into
`engine/nav/CaveSimulationNavigation.h` during `GameModule::setup` using
`CaveGameplayRuntimeFrame::installNavigationInSimulation`. This registers
persistent `CaveRouteFollower` **schema v2**, `CaveRouteIntent` v1,
`caverns.navigation.repath` and `caverns.navigation.follow` as ordered
`Simulation::addSystem` fixed-tick systems and a rollback restore hook.
`assignCaveGoal` writes a goal and backend name into a real ECS entity.
Deterministic repath uses at most the configured number of entities per
simulation tick, with per-agent cooldown and bounded solver visits. On source
or environment epoch changes the agent marks `needsRepath`, and on stale
native collision it does not cross the obstacle. The host MUST provide a
same-epoch native collision callback; GameClient visual previews cannot do so.

`engine/nav/CaveMovementPhysics.h` provides an optional adapter to the
game's real `SpatialWorld::cast/blocked` (deterministic `Fixed`) and
`physics::Scene::raycast/overlapCapsule` (Box3D float, SERVER-AUTHORITY ONLY,
not deterministic across independent lockstep clients). Either way it uses
the original native collision owners, not a duplicate cave physics scene.

`engine/world/TerraWeaveCavernsNavigationEnvironment.h` provides
read-only NEREID `lib::nereid::sampleWater` and PHYSIS
`physis::environmentAt` callbacks from a copied immutable ECS World
snapshot. Swim passability follows published water state; snow, wetness and
ice affect integer traction costs. `CaveNavigationPublication` now tracks
`environmentRevision` independently of authored SDF `sourceRevision`,
and `CaveGameplayRuntimeFrame::publishNavigationEnvironmentFromSimulation`
allows a fixed-tick owner to publish that immutable state with dirty bounds.
All such notifications invalidate the derived solver caches.

`engine/nav/CavePathQueryCache.h` memoizes successful A*/HPA/Flow/Hybrid
route results with bounded LRU (256 entries / 8MiB default) and a key
including source revision, environment epoch, solver, endpoints, full agent
profile and visit budget; `Custom` solvers bypass this cache. Native
`CaveNativeNavMeshCache` reuses the actual nav::Mesh build across queries
within an identical SDF/environment epoch and agent/config tuple (max four
instances by default). A failed physics step, new SDF, water/weather epoch,
stream residency update, rollback or graph-policy change invalidates
relevant cache data. These are NONAUTHORITATIVE derived structures and never
belong in the game checksum.

The default `GameCore` is not modified to unconditionally install cave
navigation, since many projects and the Editor Play presentation do not
own a compatible native physical collision epoch. Explicit opt-in from
the ACTUAL game authority is required; any route/component saves created
with schema v1 require an explicit migration to v2. Stage43 source changes
have not been compiled or runtime-tested under the features-first policy;
see `docs/agent-handoffs/caverns-stage43-2026-11-24.md`.

## Caverns Stage44 — ActorHost single motor and persistent 3D regions

`engine/actor/ActorModule.cpp` now supports explicit
`map.cavernsNavigation.enabled` on a world-space game using
`actor::ActorHost`. With an authored `terraweaveCaverns` SDF and
1..64 bounded `cavernsNavigation.collisionChunks` triplets, the owning
host generates initial full-resolution cave meshes, derives triangles
through `prepareCaveVolumeCollision`, and adds them to its **existing**
`SpatialWorld collision_` before character spawn and motor setup.
No additional CharacterBody/Transform motor or genre-specific physics
world is created. Native Box3D and live Carve/Fill collision replacement
require the Stage35 exclusive owner publication path separately.

`nav::assignCaveActorGoal(world,actor,goal,profile,backend,speed,budget)`
and `ActorHost::navigateCaveActor(sim,actor,...)` write the same persistent
Stage43 `CaveRouteIntent` and `CaveRouteFollower` into actual
`actor::CharacterBody` entities (not the Transform-only follower).
In the **existing** ActorHost fixed-tick character loop,
`repathCaveActorRoutes` selects an integer-revision route; the
`steerCaveActorMotor` adapter converts the next waypoint into a normal
`actor::Input` before `moveActor` or `moveActorModes`. Only the
original character motor advances physical state, preserving collision,
gravity, stance and weapon action semantics. A map without the explicit
opt-in retains existing ActorHost and FPS bots unchanged.
`ActorHost::setCaveRouteResolver` permits a named custom C++ solver
callback over an owned frozen SDF source; no NavMesh inheritance needed.

`engine/nav/GraphRegionCache.h` is a **shared reusable derived cache**
of deterministic graph passability, adjacency and HPA portal edges.
`refresh(graph,dirtyNodeIDs)` re-samples the affected nodes and
rebuilds outgoing portals for their corresponding clusters; a caller
includes a one-cell halo for incoming links. The cache keeps bounded
complete reverse Dijkstra/Flow integrations per goal with LRU reuse,
discarding them when the graph changes. It uses existing
`nav::findGraphDijkstra`, not a cave-specific solver clone. Full
incremental shortest-path repair inside an affected flow field is
**not** implemented yet: after a dirty event the next query rebuilds
that goal field. `CaveNavigationRegionCache` adapts this reusable
cache to signed centimetre 3D cave cells and half-open SDF dirty
bounds. `CaveGameplayRuntimeFrame::enableRegionalNavigation` registers
`regional.astar`, `regional.hpa`, `regional.flow`,
`regional.hybrid`. An optional `map.cavernsNavigation.region`
lets ActorHost reuse those paths without a full SDF graph build
per route; fallback is the Stage42 bounded general 3D search.

`ActorHost::acceptCommittedCaveNavigation(provider,change,exclusive)`
may switch to a new immutable source and refresh regional data **only
after** the actual native SpatialWorld collision change has been
published under a held readers/simulation barrier. This API does not
itself replace Box3D or GPU owners. Both authored and environment
revisions remain authoritative repath constraints.

Stage44 source commits are not proof of compilation, gameplay parity or
all-owner rollback. Native builds and test campaigns remain deferred by
the user's features-first request. See
`docs/agent-handoffs/caverns-stage44-2026-11-24.md`.

## Stage45 architecture correction — ActorHost coupling retired (2026-11-24)

The Stage44 ActorHost-specific native cave integration documented above was **reverted** in the feature branch after review of the engine's genre-neutral architecture. Do not use it as a current API. Both `engine/actor/ActorModule.h` and `engine/actor/ActorModule.cpp` were returned to their pre-Stage44 content; `CaveActorMotorSteering.h` and ActorHost-specific collision catalog/commit headers were deleted.

Use these **common C++ interfaces** instead:

- `engine/world/TerraWeaveCavernsSpatialRevisionOwner.h` owns bounded versioned cave collision leases in an existing `world::SpatialWorld`; staged changes use an independent complete snapshot and an exclusive-owner publish barrier. It does not import ActorHost or assert any automatic Box3D, NavMesh, or GPU publication.
- `engine/nav/CaveSimulationNavigation.h::installCaveRepathSimulation` opts into the authoritative ECS route-intent/persistence/replanning systems **without installing a movement controller**.
- `engine/nav/CaveEcsSteering.h::prepareCaveSteering` takes a shared ECS entity-position callback and produces a versioned `CaveSteeringRequest`; a project's existing walking/flying/swimming/vehicle motor consumes it. `installCaveSteeringSystem` is an optional fixed-tick producer. Only the selected motor mutates physical position; do not also install the direct Transform follower for the same entity.

Every algorithm still consumes the immutable Cave SDF provider, not a genre-specific entity representation. See `docs/agent-handoffs/caverns-stage45-2026-11-24.md`. These changes are committed source awaiting native build/verification.
