# TerraWeave Caverns — Stage 42 shared graph solvers and ECS navigation (2026-11-24)

## Provenance

Repository: `chromealex/AIVERSE`
Branch: `feature/terraweave-caverns-core-20261123`. **No main merge.**

GitHub commits in this stage:
- `fe8649884d68026aaa0120573a58981c2ab7fa06` — new `engine/nav/GraphSearch.h`: generic bounded deterministic integer A*/Dijkstra and directed reverse-flow integration
- `4b6be6a3738b61e6631e097e9635fb1722fd93de` — new `engine/nav/GraphHierarchy.h`: generic HPA-style clustered portal abstraction/corridor refinement using common graph search
- `a9dfd508f189ad6daf4d4390b87650fc08a6b588` — new `engine/world/TerraWeaveCavernsGraphSolvers.h`: graph projection, A*/HPA*/Flow/Hybrid dispatch from shared algorithms
- `b899f379fe394e15166d93a9ba98c43b63171865` — `engine/world/TerraWeaveCavernsRuntimeFrame.h`: register four shared graph solver backends by default
- `44e1495614e8f131d0dc958567ca99e860b4f621` — new `engine/world/TerraWeaveCavernsNativeNavSolver.h`: actual `engine/nav/NavMesh.h` multi-span query adapter
- `bfc6e3b2bcdc2e4087acf301cffabb9b9ae62192` — new `engine/nav/CaveEcsRouteFollow.h`: real shared ECS route/Transform update with strict native collision callback
- `228750e8334a09655639a9644ac8b7a61011177c` — runtime methods to register NavMesh and invoke ECS locomotion on published SDF
- `055a2932fcbc5f33b7cb169a72eeae8e4457c9cd` — `engine/world/TerraWeaveCavernsNavigationGraph.h`: adapter from CaveGraphEdge to `nav::GraphEdge`
- `79b4e786ec6e118047e2f10441a315e1565c4131` — `engine/nav/CaveEcsRouteFollow.h`: transfer a confirmed `CavePathResult` onto an ECS entity
- `940e30d3ae5be25a69f986017a1f536af26fd4ff` — `engine/world/TerraWeaveCavernsGraphSolvers.h`: integer abs include
- `da55ca415c284cafdb7dcee9eedb6eb650d59dd4` — `skills/world/aiverse-pathfinding/SKILL.md`: generic graph routing and ECS movement guidance
- `3c7556bf0eb583272c5db1125dd20366a5f58209` — `skills/world/aiverse-terraweave-caverns/SKILL.md`: Stage42 guidance
- `7b9322af525be8cf86e6d73909c06f4eb84d498f`, `d98f98dabef78c4aa73c702058ff6beaf3c88788`, `bd9d8f767982ece0c44e8a2e2bef613b0b3aaefd` — overflow-safe 3D node budget and grid dimensional validation
- `a6db84a7b757e4d99bb9fea4bcbc6f287f9d4aa6` — `docs/pathfinding.md` source/API documentation
- `ad3dc725327df7b10459ea6abe6bdcd10b3fc499` — `AGENTS.md`: algorithm ownership and tick semantics
- This handoff is a subsequent commit; inspect current branch HEAD for tip SHA.

## What changed

### 1. Shared reusable engine algorithms

The user explicitly requested more than NavMesh: all existing navigation families including custom pathfinding must support Caverns. Previous Stage41 offered a common data provider but NO actual graph search; the older `game::MapGrid` algorithms require 2D grid layer storage and could not directly search 3D cave nodes.

Stage42 introduces **new shared genre-independent graph search implementations in `engine/nav`** rather than copying algorithms into a Caverns-specific module. `GraphSearch.h` implements `nav::findGraphAStar(graph,start,goal,maxVisits,heuristic)`, `nav::findGraphDijkstra(...)` and `nav::buildGraphFlow(graph,goals,maxVisits)`, with stable node ID ties, bounded graph size/visits, nonzero integer costs and reverse adjacency for asymmetric flow-field integration.

`GraphHierarchy.h` implements `nav::findGraphHierarchical` for ANY graph with a caller-provided clusterOf function. It groups nodes, constructs directed portal/cross-cluster edges, performs an abstract search with the shared generic graph solver, refines within a coarse corridor and falls back to a whole-graph search when necessary. This is an **HPA-style** approximation and not a claim that the exact historical `MapHpa.cpp` source was transplanted unchanged.

### 2. 3D Caverns solver bindings

`CaveNavigationGridView` now supports `neighbors(node,vector<nav::GraphEdge>&)`, preserving the Stage41 six-axis topology and occupancy rules. `solveCaveGraph` builds a bounded 3D query grid around the start and goal under `CaveGraphSolverSettings` (cm spacing, max nodes, HPA cluster size, hybrid size threshold, margin), and dispatches:
- `astar`: shared engine graph A*, Manhattan admissible edge heuristic
- `hpa`: shared engine cluster-portal hierarchical route with full-graph fallback
- `flow`: shared engine backward multi-goal directed flow integration and descending route
- `hybrid`: direct graph A* or HPA-style corridor based on bounded region size

`registerCaveSharedGraphSolvers(router,settings)` registers these four actual callbacks; `CaveGameplayRuntimeFrame` performs this registration in its constructor. `setGraphSolverSettings` reconfigures their bounded region limits. All results carry the exact committed SDF `sourceRevision`.

`TerraWeaveCavernsNativeNavSolver.h` adds `findCaveNativeNavPath`/`registerCaveNativeNavSolver` using the actual `engine/nav/NavMesh.h::Mesh` and Stage41 `CaveSharedNavSource`; `CaveGameplayRuntimeFrame::registerNativeNavMeshSolver(cfg)` exposes explicit registration. It handles ground/multi-span climbing, not arbitrary 3D swimming/flight. The adapter currently builds the native mesh on demand and does not yet cache incremental tile refresh, so repeated queries may be expensive.

`CavePathRouter` keeps all Stage41 user-custom solver callbacks unchanged: custom algorithms can consume the exact same `CaveNavigationDataProvider` source and register with unique names and area policies without NavMesh inheritance.

### 3. Actual shared ECS component bridge

New `engine/nav/CaveEcsRouteFollow.h` uses the existing shared `aiverse::World`/ECS, `Transform` and `Fixed`. `CaveRouteFollower` stores current waypoint, all 3D waypoints, source revision, profile, speed, arrived and needsRepath flags as component state, **not** a separate FPS/RTS actor list.

`nav::assignCaveRoute(World&,Entity,CavePathResult,profile,speedCmPerTick)` copies a published solver result onto a valid ECS entity; `nav::updateCaveRouteFollowers(World&,provider,collisionCallback)` performs bounded signed integer-centimetre waypoint following with deterministic Fixed transform updates and prior transform for animation interpolation. If a route revision is stale, `needsRepath` is set and movement is refused. The new position is checked against BOTH the shared published SDF via `provider.canTraverse` AND an external **mandatory** same-epoch native physics callback to avoid dynamic collisions. Without that callback, no movement is performed.

`CaveGameplayRuntimeFrame::updateEcsRouteFollowing(world,callback)` invokes this only if there is an active published source revision and no `RecoveryRequired` or undelivered navigation invalidation.

### What is NOT yet wired

- **Legacy `game::MapGrid` 2D algorithms unchanged.** `MapGrid::astarCells`, `MapHpa.cpp`, `FlowField.cpp`, `MapGrid::hybridPath` have NOT been refactored to accept the new generic 3D graph. New shared algorithms in `engine/nav` fulfill the 3D route functionality without copying these specialized 2D routines into Caverns, but do not automatically migrate old MapGrid users. A later explicit migration to generic graph inputs is necessary if one algorithm implementation must eventually serve BOTH legacy 2D and cave 3D.
- **Actual game tick integration remains opt-in.** No change to `GameCore::setup` registers an ECS cave follower system; the existing GameCore may not own matching Box3D/SpatialWorld physics, and injecting movement there without same-epoch native collision authority would be unsafe. The actual game/authority host must register a fixed-tick callback, route/repath job scheduling and deterministic serialization/hash/replay for authoritative route state.
- NEREID flooding, PHYSIS hazard costs and climbing callbacks still require the actual published external world epoch; the Stage41 provider does not invent them.
- The query region is bounded for memory safety. Long paths whose AABB exceeds the configured `maxNodes` currently fail rather than streaming a multi-region hierarchy across unloaded world chunks; a larger-region streaming routing feature is pending.
- HPA-style hierarchy is built from scratch per query; flow reverse adjacency is also built per query. Do not claim high-traffic performance or cache parity with existing optimized MapHpa/FlowField pipelines before profiling.
- GPU/Box3D/NavMesh all-owner atomic commit and actual client/server authority have not been fully completed; Stage40 GameClient remains visual-only. NavMesh adapter may be instantiated by the native owner but no browser/frontend path automatically controls authoritative NPC movement.

## Verification actually conducted

Following user's **features first, tests later** instruction, no native C++ build, CMake/CTest, NavMesh path query, A*/HPA/Flow/Hybrid solver run, ECS simulation replay/hash, Box3D physics, spatial collision or GPU test was executed for Stage42. All file writes are confirmed by the GitHub connector with commit SHAs; those confirm publication, not source compilation or runtime results. Do not claim verified feature completeness.

## Next stage

1. Wire generic shared graph solvers into the actual authoritative ECS Simulation/GameModule with exact native physics/SpatialWorld collision and route serialization/replay, including a deterministic repath queue and NEREID/PHYSIS movement costs.
2. Implement cached/reusable 3D HPA/flow/NavMesh data over streaming cave regions with graph revision and dirty-region invalidation, without rebuilding the entire graph per route.
3. Finish remaining native physical publication/recovery and STRATA/GPU pages. After requested feature completion, run full compilation and all backend/behavioral QA before any main merge.
4. Keep updating `skills/world/aiverse-terraweave-caverns/SKILL.md` and `skills/world/aiverse-pathfinding/SKILL.md` and an independent dated handoff after each stage.
