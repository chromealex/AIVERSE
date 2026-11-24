---
name: aiverse-pathfinding
description: Use AIVERSE pathfinding (grid A*/HPA*, flow fields, navmesh, hybrid) from any project or native module.
---

# AIVERSE navigation

Resolve paths relative to the engine root. Read `libraries/catalog.json`,
`docs/libraries.md` and `docs/native-modules.md` before integration.

## Select and integrate

- Layer: **simulation**. Current coverage: Existing pathfinding and flow fields.
- Read `docs/pathfinding.md` as the current API authority. No Lua implementation is required.
- Any-angle paths: rules or map `movement.pathfinding: "navmesh"` (flat maps) uses the genre-independent `engine/nav` mesh (`docs/navmesh.md`); grid A*/HPA* stay the fallback and the default.
- Big or mixed maps: `movement.pathfinding: "hybrid"` (HPA* cluster graph, navmesh in open clusters, grid A* in maze clusters, flow fields on the same graph). Measure a map with `aiverse_headless --bench-nav N` before choosing.
- Reuse movement commands; do not move entities from view scripts. Test blocked goals, arrival and planets separately.
- Use the existing C++ engine API documented in the source above. This reference entry is not a standalone library target.
- Use `examples/native/libraries` as a compiling project-local example.
- Pin with `python3 tools/libraries/catalog.py pin navigation --project /absolute/game` for C++ foundation entries; verify with the same command using `verify` before upgrading.
- Do not copy algorithms into the game. Extend the common library if a reusable operation is missing.

## Contracts

IDs and indices are zero-based unless documented otherwise; task worker IDs are positive.
Keep definitions immutable. Store persistent gameplay values in NativeState values/arrays,
reconstruct transient structures locally and write changes back before returning from a tick.
Library containers do not register ECS pools, hash themselves or serialize automatically.
Never retain references across rollback. Validate commands before calling helpers.
No floating-point scores or platform RNG in simulation. Supply sim.rng() through an explicit adapter
with the documented integer range. Resolve ties deterministically; sort entity inputs by index.
Presentation.h is presentation only and must not drive simulation outcomes.

## Verify

Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py` after integration changes.
For simulation integration run full engine tests and headless restore/template checks from AGENTS.md.
Coverage is deliberately bounded by the catalog; do not claim BT/GOAP, flow solvers or the entire
category is implemented when only its foundational API exists.

Runtime library: none. This recipe integrates an existing engine subsystem or runs host tools;
it does not provide a separate runtime implementation. Pin the engine revision for that owner.

## TerraWeave Caverns shared navigation (Stage41, 2026-11-24)

For cave volume paths, read `skills/world/aiverse-terraweave-caverns/SKILL.md`,
`engine/world/TerraWeaveCavernsNavigationData.h`,
`engine/world/TerraWeaveCavernsNavigationGraph.h`,
`engine/world/TerraWeaveCavernsNavigationPublication.h`,
`engine/world/TerraWeaveCavernsPathRouter.h`, and
`engine/world/TerraWeaveCavernsNavigationAdapters.h`.

- Cave SDF and its immutable revision are **input data**, not a replacement A* or another duplicated NavMesh. `CaveNavigationDataProvider::sample(point,agentProfile)` exposes clearance, floor support, water flag, area class and integer cost; `canTraverse(a,b,profile)` validates transitions and intermediate samples. Agent profiles include ground, flying (3D void), swimming (requires NEREID callback), climbing (project/physics callback) and custom (registered callback). The 3D cell graph supports signed X/Y/Z neighbors, including vertical transitions.
- `CaveNavigationGridView` provides fixed deterministic 3D cell IDs, `passable(id)`, `neighbors(id,vector<CaveGraphEdge>&)` with bounded integer costs, and `portals(clusterWidth)` for HPA* graph construction. Feed these callbacks to an existing shared A*/HPA*/FlowField/Hybrid solver or a custom solver. Do not duplicate their search algorithms inside Caverns. **Important:** existing `game::MapGrid::findPath/flowAcquire/hybridPath` are presently bound to MapGrid's 2D cell state and do not directly accept an arbitrary 3D callback graph. An explicit native solver adapter is still needed to route these exact implementations over the cave graph; do not claim this is already wired into MapGrid movement.
- `CaveSharedNavSource` adapts the common SDF/walkability provider to the real native `nav::Source` multi-span interface, which existing `nav::Mesh` can build. It is suitable for ground/climbing surfaces only, not arbitrary free-flight volumes. `CaveNavSource`/Stage29 authored-nav stage remains available for existing integrations, with shared indexed SDF.
- `CavePathRouter::registerSolver(name,backend,callback)` routes path queries to an actual existing or custom solver. The callback receives a provider from a **committed** source revision, and its result must report the same revision. Built-in algorithm names `AStar`, `HPAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom` are backend IDENTIFIERS, not claims that all six existing engines have been linked to the router automatically.
- `CaveNavigationPublication` notifies subscribers (`AStar`, `HierarchicalAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom`) of a revisioned dirty region **after** actual Box3D/SpatialWorld/nav/GPU game publication. Streamed in/out chunks also invalidate graph/cache topology at the same authored SDF revision. Delivery errors retain pending events; the router refuses to answer from potentially stale derived caches until consumers catch up. Each solver chooses its own dirty-grid, portal, flow-seed or native tile refresh policy.
- NEREID flooding and PHYSIS hazards are provided via read-only, same-epoch external callbacks. Never make solver caches depend on unversioned mutable water/physics state or pick a different source revision than the authoritative SDF.
- Tests and native linking remain postponed until all requested features are completed, per user. See `docs/agent-handoffs/caverns-stage41-2026-11-24.md` for ownership, remaining integration work, and accurate non-tested status.

## Stage42 — shared 3D graph solvers and ECS path following (2026-11-24)

Read `engine/nav/GraphSearch.h`, `engine/nav/GraphHierarchy.h`,
`engine/nav/CaveEcsRouteFollow.h`,
`engine/world/TerraWeaveCavernsGraphSolvers.h`,
`engine/world/TerraWeaveCavernsNativeNavSolver.h`, and
`engine/world/TerraWeaveCavernsRuntimeFrame.h`.

- **Algorithms live in shared engine/nav, NOT Caverns.** `nav::findGraphAStar` and `nav::findGraphDijkstra` are deterministic integer-cost general graph searches with bounded visits and node-id tie breaking. `nav::buildGraphFlow` runs reverse-directed Dijkstra on the same adjacency, producing node integration values and descending next pointers. `nav::findGraphHierarchical` builds a bounded abstract portal graph with cluster identifiers supplied by the caller, routes with the common graph solver, refines within a corridor, and falls back to a complete graph search if necessary. These are reusable across genres and graph owners.
- `CaveNavigationGridView::neighbors(node,vector<nav::GraphEdge>&)` bridges the already existing cave 3D topology to those shared algorithms. `solveCaveGraph(CavePathQuery,provider,CavePathBackend,settings)` constructs a bounded 3D region from the query and calls shared `engine/nav` implementations. `registerCaveSharedGraphSolvers(router,settings)` publishes named real backends `astar`, `hpa`, `flow`, `hybrid`. `CaveGameplayRuntimeFrame` registers them automatically on construction and supports `setGraphSolverSettings`. All paths retain the exact committed source revision; custom solver callbacks remain supported.
- `registerCaveNativeNavSolver(router,config,"navmesh")` binds the **existing** `nav::Mesh` to `CaveSharedNavSource` and returns ground/climbing paths, not arbitrary 3D flight. `CaveGameplayRuntimeFrame::registerNativeNavMeshSolver(config)` exposes explicit native registration. Current adapter builds a bounded `nav::Mesh` on demand; cache and incremental tile refresh are the real game host's performance responsibilities.
- `nav::CaveRouteFollower` is an opt-in shared ECS component storing versioned 3D waypoints, next index, speed and traversal profile. `nav::assignCaveRoute(world,entity,path,profile,speed)` stores an actual committed result on the ECS entity, not in a genre-specific actor vector. The authoritative Simulation owner explicitly registers `nav::updateCaveRouteFollowers(world,sharedProvider,realPhysicsCallback)` at its tick. The update uses signed integer-cm interpolation / Fixed::fromRatio transforms and a matching SDF revision. A stale route sets `needsRepath` and refuses motion. A real host physics callback is required for **all movement**, checking dynamic obstacles, Box3D and SpatialWorld against the same committed epoch. No callback means no movement.
- `CaveGameplayRuntimeFrame::updateEcsRouteFollowing` safely exposes the ECS update only with an active published SDF and no pending navigation notifications / recovery state. **The actual game module must call it on its deterministic simulation step.** This stage does not inject a new system into `game::GameCore`, because the real Box3D / SpatialWorld owner and deterministic ECS state serialization/hash contract must be supplied by the game authority.
- **Compatibility and limits:** legacy `game::MapGrid` 2D `astarCells`/`MapHpa`/`FlowField.cpp`/`hybridPath` are NOT refactored yet; introducing the common reusable `engine/nav` graph solvers allows cave 3D support without modifying stable 2D gameplay behavior. Do not claim legacy pathfinder code has been physically migrated. The 3D HPA route uses a bounded coarse-cluster portal approximation with whole-graph fallback, and flow builds reverse adjacency per query; both need performance caching and end-to-end acceptance before optimizing for huge worlds. No library duplicate under Caverns.
- User directive remains features first, tests afterwards. Stage42 code is NOT compiled or acceptance-tested, so do not claim native game routing or ECS locomotion is verified. See `docs/agent-handoffs/caverns-stage42-2026-11-24.md`.

## Stage43 — fixed-tick ECS repath, NEREID/PHYSIS and caches (2026-11-24)

Read `engine/nav/CaveSimulationNavigation.h`,
`engine/nav/CaveMovementPhysics.h`,
`engine/nav/CavePathQueryCache.h`,
`engine/world/TerraWeaveCavernsNavigationEnvironment.h`,
`engine/world/TerraWeaveCavernsNativeNavMeshCache.h`, and
`docs/agent-handoffs/caverns-stage43-2026-11-24.md`.

- **Real Simulation hook, opt-in:** Call `CaveGameplayRuntimeFrame::installNavigationInSimulation(sim,realCollisionCallback,budgets)` *during authoritative GameModule/Simulation setup*, not visual-only GameClient. It delegates to shared `nav::installCaveNavigationSimulation`, registers `CaveRouteFollower` persistent schema **v2** and `CaveRouteIntent` v1, installs ordered deterministic fixed-tick `caverns.navigation.repath` and `caverns.navigation.follow` ECS systems, and a rollback state-restored hook. New `assignCaveGoal(world,entity,goal,agent,algorithm,speed,maxVisits)` creates/updates the authoritative route intent and follower on the ECS entity. The routing system processes at most `maxRepairsPerTick` entities per tick and honors per-entity retry cooldowns in deterministic pool order. Unsupported, stale, failed or blocked routes never bypass actual geometry.
- **Persistent & rollback-safe:** Route points, profiles, sourceRevision, environmentRevision, cursor, speed, and repath state are encoded field by field with `Archive` in the true ECS component schema. Version changed to v2; old v1 saves need an explicit migration if supported; do not pretend they are binary compatible. Rollback marks routes stale when SDF or environment epochs mismatch and invalidates derived route memoization. The game module must still validate incoming navigation/goal commands and own the native simulation-to-physics publication barrier.
- **Native collision adapter:** `makeCaveMovementPhysics(spatial,optionalBox3D,selfBody,settings)` checks conservative swept AABB and destination bounds against authoritative `world::SpatialWorld` Fixed-point collision. Optionally uses the SAME game-owned `physics::Scene` raycast/overlapCapsule and ignores the actor's body; **Box3D float queries are appropriate only to an authoritative host, not cross-platform deterministic lockstep**. The adapter is a required real callback for follower movement; a missing callback means no movement.
- **NEREID and PHYSIS:** `makeCaveEnvironmentCallbacks(CaveEnvironmentSource{sourceRevision,simulationTick,frozenEcsWorld},costSettings)` calls existing `lib::nereid::sampleWater` and `physis::environmentAt` on a read-only immutable ECS World snapshot. Water determines swimming availability and optionally adds surcharge; PHYSIS traction (wetness, snow, ice) changes ground/climb integer routing costs. No second water/weather simulation is added. The authoritative runtime calls `publishNavigationEnvironment(frozenWorld,environmentEpoch,dirtyBounds,settings)` or `publishNavigationEnvironmentFromSimulation(sim,dirtyBounds,settings)` **after the sim's native committed tick**. This updates an independent environment epoch at the same authored SDF revision and notifies all algorithm consumers.
- **Cache and invalidation:** `nav::CavePathQueryCache` is a bounded LRU of successful route results keyed by exact SDF revision, environment epoch, solver name, start/end points, full agent profile and visit budget. Defaults 256 routes/8MiB waypoint bytes. A*/HPA-style/Flow/Hybrid results are reused; custom `CavePathBackend::Custom` bypasses memoization by default. `CaveNativeNavMeshCache` reuses an actual `nav::Mesh` for the same SDF/environment source, nav config and agent profile (default four mesh owners), rather than rebuilding on every query. The Stage41 publication bus invalidates both caches after Carve/Fill, streamed residency changes and NEREID/PHYSIS updates. A dynamic physical collision blockage also invalidates route memoization; Simulation rollback invalidates it as well.
- **Important limits:** HPA portal construction and Flow reverse-distance integration are still computed on a cold unique query; Stage43 memoizes completed routes and native NavMesh instances, NOT all HPA cluster/flow internals. The actual game host must opt in and provide matching physics, serialized ECS state and controlled simulation barriers. The normal Stage40 GameClient remains visual-only, and `net::Server` remains an input relay. Do not claim automatic FPS/RTS locomotion or native compile success. The user requested features before tests; no CMake/CTest or native runtime checks were run in Stage43.

## Stage44 — ActorHost single-motor cave steering, regional graph and Flow/HPA caches (2026-11-24)

Read `engine/nav/CaveActorMotorSteering.h`,
`engine/nav/GraphRegionCache.h`,
`engine/world/TerraWeaveCavernsNavigationRegionCache.h`,
`engine/actor/ActorModule.h`,
`engine/actor/ActorModule.cpp`, and
`engine/world/TerraWeaveCavernsRuntimeFrame.h`.
See `docs/agent-handoffs/caverns-stage44-2026-11-24.md`.

- **Single motor in ActorHost.** Actor characters use `CharacterBody`, NOT shared `Transform`. Never call Stage43 `updateCaveRouteFollowers(World,Transform)` on an ActorHost entity. Instead `nav::assignCaveActorGoal(world,entity,goal,agent,algorithm,speed,maxVisits)` installs existing `CaveRouteIntent` and `CaveRouteFollower` ECS components, and `nav::repathCaveActorRoutes` resolves them from `CharacterBody.pos`. `nav::steerCaveActorMotor` converts next route waypoint to ordinary `actor::Input` (yaw, forward) in the existing ActorHost tick, **after** stale network input clearing and **before** existing `moveActor/moveActorModes`. Only the existing CharacterBody motor mutates position, gravity, crouching, physics, locomotion, and combat state. Route speed is scaled relative to the existing motor's configured per-tick speed; no double movement.
- **Real ActorHost spatial collision.** Opt-in map `cavernsNavigation.enabled=true` requires versioned authored `terraweaveCaverns` and an explicit bounded `cavernsNavigation.collisionChunks` array of [x,y,z] keys (1..64). The native ActorHost's setup generates immutable full-resolution SDF cave meshes from Stage39, converts them through existing Stage27 `prepareCaveVolumeCollision`, and adds triangles to ActorHost's **existing** `world::SpatialWorld collision_` before character spawn validation. Collider IDs start at reserved `0x60000000` and remain fixed in this initial setup. This is a **static opt-in initial-world** integration, not Stage35 live all-owner dynamic body publication. It does NOT create a second Physics/Box3D Scene or genre-specific pathfinding system.
- `ActorHost::navigateCaveActor(sim,entity,goal,profile,algorithm,speed,maxVisits)` exposes a C++ authoritative command integration hook. No unvalidated new network wire command is introduced. Only actual actor entities with `CharacterBody` can receive the goal. The map opt-in registers `caverns.route.intent` v1 and `caverns.route` v2 persistence schemas. Existing FPS bot control remains default unless an explicit cave route is attached; input steering is installed on that actor only.
- **Custom algorithms:** `ActorHost::setCaveRouteResolver(resolver)` lets the game authority inject any registered/custom solver callback (with exact frozen source). In the default case `astar`, `hpa`, `flow`, `hybrid` resolve via the shared Stage42 `engine/nav` solvers. Custom callbacks must return the exact source revision and must not retain a borrowed provider pointer; they run on the owning fixed tick.
- **Region cache.** `engine/nav/GraphRegionCache.h` is shared, genre-independent and owns cached node passability, directed neighbors, region membership, HPA portal adjacency and bounded goal flow integrations. `build(graph,regionForNode)` is one-time. `refresh(graph,changedNodes)` validates and replaces only explicitly dirtied nodes, rebuilds outgoing portals for their regions and invalidates cached flow integrations. `hpa` reuses portal adjacency for abstract routing and refines using shared `nav::findGraphDijkstra`, with full-graph fallback; `flow` reuses a complete reverse field keyed by goal and bounded LRU. No duplicate HPA/Flow algorithm under Caverns. **Flow invalidation is whole-field** after dirty region; only graph/HPA portals have incremental updates, not incremental Dijkstra relaxation.
- `CaveNavigationRegionCache` maps Stage41 half-open signed cm dirty bounds into a 3D grid plus one-cell halo via `caveGraphDirtyRange` and feeds the shared graph cache. `CaveGameplayRuntimeFrame::enableRegionalNavigation(grid,profile,clusterCells,maxFlows)` exposes named `regional.astar`, `regional.hpa`, `regional.flow`, `regional.hybrid` solvers for a configured loaded region; Stage41 publication callbacks refresh it after confirmed SDF/environment/stream changes. An optional `cavernsNavigation.region` on ActorHost maps similarly configures a persistent region source; its resolver falls back to the shared general bounded solver when the region cannot answer.
- **Live ActorHost updates require actual owner barrier.** `ActorHost::acceptCommittedCaveNavigation(provider,change,exclusive)` refreshes its derived graph and switches immutable SDF source ONLY after another native owner already replaced the matching SpatialWorld collision (and relevant Box3D/NavMesh/GPU if required). A predicate returning true without a held exclusion does not prove a safe publication. Stage44 does not insert Stage35 live cave edits into ActorHost automatically.
- **Remaining:** initial ActorHost cave collision source is static; automatic runtime Carve/Fill collider replacement, ECS command wire mapping, multi-actor motor profile support (flight/swim/climb), truly incremental Flow Dijkstra, streaming region residency, and full cross-owner recovery still need completion. Older MapGrid 2D specialized algorithms are not refactored: Stage42 shared general graph algorithms are used instead.
- Per user's features-before-tests instruction, no Stage44 build, CMake/CTest, native editor/GameClient, ActorHost tick, GPU or Box3D test was performed; all GitHub commits prove source publication only. Keep a separate Stage44 handoff and do NOT merge main until acceptance.

## Stage45 correction — no ActorHost dependency (2026-11-24)

**Supersedes Stage44 ActorHost guidance.** All Stage44 modifications to `engine/actor/ActorModule.cpp` and `ActorModule.h` were reverted to the Stage43 versions; `engine/nav/CaveActorMotorSteering.h` was deleted. The ActorHost-specific Stage45 collision catalog/commit headers were also deleted. **Do not include actor/FPS or create a second character motor in Caverns or common navigation.** Use `engine/world/TerraWeaveCavernsSpatialRevisionOwner.h` as a generic bounded, versioned SpatialWorld candidate owner: prepare the entire collision scene in a private copy and publish only with actual native world quiescence. It is independent of genre/actor ECS schema and does not imply Box3D/NavMesh/GPU owners committed.

`engine/nav/CaveEcsSteering.h` adds `CaveSteeringRequest` and `prepareCaveSteering`: ECS outputs a versioned desired-motion intent, not a position. The *actual project movement controller* consumes the intent and remains the sole physics position owner. `installCaveSteeringSystem` is an opt-in fixed-tick producer. `nav::installCaveRepathSimulation` registers path persistence, deterministic repath and rollback handling WITHOUT installing the Transform mover. Projects choosing the old direct Transform follower can still opt into `installCaveNavigationSimulation`; they MUST NOT register both movement consumers for the same entity. Keep custom algorithms via `CavePathRouter` and geometry via the immutable SDF provider. Read `docs/agent-handoffs/caverns-stage45-2026-11-24.md` for limits; native compilation/tests still deferred.

## Stage46 — motor-neutral ECS integration (2026-11-24)

Use `nav::installCaveMotorNavigationBefore(sim, hooks, position, motorSystem, maxEntities)` from `engine/nav/CaveEcsSteering.h` after the real movement system is registered, or `CaveGameplayRuntimeFrame::installMotorNavigationInSimulation`. The installer inserts repath and steering in that order immediately before the uniquely named existing motor and rejects duplicate navigation installation. No actor module, new motor, or position/velocity writer is created. `CaveWorldPositionSource` reads an optional signed-centimetre point from the current const ECS World and entity; do not capture component pointers across rollback. `readCaveTransformPosition` is the default for direct Transform following only; custom positions need no Transform component or collision callback for route search. Actual movement/contact checks remain with the motor.

Use `assignCaveMotorGoal` on an already authorized command/tick to create the route intent, follower and steering request; `cancelCaveMotorGoal` clears intent, `reportCaveMotorBlocked` invalidates the request and queues repath on matching epochs. Consume `caveMotorRequest` only after the producer has run this tick and only against the same committed provider. A missing request means no cave steering. Dynamic-obstacle cache invalidation remains the existing authority's responsibility. Each tick clears old requests, including those outside the rotating deterministic steering budget, missing route, canceled goal, and unavailable published source. Both SDF and environment revisions must match, and waypoint count and coordinates are bounded. If the budget is below entity count, only that tick's selected slots receive movement intent. Restoration invalidates derived caches without rewriting restored persistent route/intent/request bytes; the next tick validates epochs before movement. The component schema versions are unchanged. The authoritative runtime accessor `gameplayNavigationSource()` refuses reads while closed, quarantined or pending nav invalidations. This is an opt-in generic host integration API, not proof of a game-specific motor connection or native world publication. Never use the visual-only preview or lockstep relay as gameplay authority.

Native compile, save/replay, actual motor, physics and GPU acceptance: NOT RUN (features-first sequence). See `docs/agent-handoffs/caverns-stage46-2026-11-24.md`.
