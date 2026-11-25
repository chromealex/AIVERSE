# TerraWeave Caverns — Stage 41 algorithm-neutral navigation handoff (2026-11-24)

## Repository and actual commits

Repository: `chromealex/AIVERSE`
Branch: `feature/terraweave-caverns-core-20261123` (**not merged to main**).

GitHub connector confirmed these Stage41 commits:
- `8af15c527cfe186dfdaa513cbb0cbf4ba69588f4`: new `engine/world/TerraWeaveCavernsNavigationData.h` shared SDF navigation input provider
- `dac983107bef24ddb71962ddf1c70c795be251bb`: new `engine/world/TerraWeaveCavernsNavigationGraph.h` deterministic 3D graph, costs and cluster portals
- `382329c5e92cf52b67f37b1dda952d48ec3b3bb1`: new `engine/world/TerraWeaveCavernsNavigationPublication.h` immutable source changes and callback subscriptions
- `afa054682a71099d464d7ea047d159cf52b616b0`: same-revision stream-residency invalidation without falsifying SDF authoring epoch
- `2443aeeec61e602a2d3e054e9b0254858e082ef5`: `engine/world/TerraWeaveCavernsRuntimeFrame.h`, publish solver invalidations only after true native commit
- `d3c4b9a8fb1ab840fd1333591ab027feebdfe3cf`: new `engine/world/TerraWeaveCavernsPathRouter.h` backend-registered callback router
- `891a92820656ab4c1c853a08bfa09d6e0c79e801`: expose registration and query through gameplay runtime frame
- `4f73a2869231abd9a23c441f0a80404b7e992136`: enforce area mask bounds and valid custom callback occupancy
- `f18763213c38aa9ef5ca5a03158f0fa3dcb0de4b`: stream notification bounds captured before moving authored mesh
- `d4a9bc4382b0df9fb79f57cf213998ee99ca62d4`: new `engine/world/TerraWeaveCavernsNavigationAdapters.h`, native `nav::Source` adapter
- `18d3aaaa87699de8b5c07ec7e995f87e6bbdd5b8`: new `engine/world/TerraWeaveCavernsNavigationInvalidation.h`, graph/HPA/native NavMesh dirty projections
- `dcc328ab1e7a521a0aab7c347807ec5089d64166`: `docs/pathfinding.md` architecture and integration limits
- `b373808562837255d0c9331d95686d4ee9bf14fa`: `AGENTS.md` shared algorithm ownership rule
- `77b28c4851f8f2a28d545b9714e92a9f14bffe0e`: `skills/world/aiverse-pathfinding/SKILL.md` documented Caverns inputs and custom solver registration
- `b5a7e167a69c995926e7ea1696cf644922cc87e2`: `skills/world/aiverse-terraweave-caverns/SKILL.md` Stage41 update
- `3de58e2e95d2a9e1036db1c53b87216bcc291270`: custom adjacency also requires intermediate committed-SDF clearance
- This handoff is committed afterwards. Read current feature HEAD for exact tip.

## Motivation / original problem

Stage27/29 Caverns exposed native `nav::Source` and nav::Mesh as the default, but AIVERSE already has A*, HPA* (MapHpa), Flow Fields, NavMesh and a Hybrid path mode and the user explicitly requires custom algorithms. Cave volumes should never force agents to use NavMesh, and a 2.5D navmesh is not a correct universal representation for free 3D flight, swimming or wall-climbing. Stage41 separates the immutable **navigation data** from the selectable solver backend.

## New interfaces

### 1. Shared density + traversal data

`CaveNavigationDataProvider(shared_ptr<const CaveSnapshotSpatialIndex>,CaveNavigationCallbacks)` keeps one immutable Stage39 SDF source and exposes `revision()`, `indexedSource()`, `sdf()`, `clearAt(point,agent)`, `hasSupport(point,agent)`, `sample(point,agent)`, `canTraverse(from,to,agent)`. `CaveAgentProfile` selects agent radius/height/step allowance, query sampling granularity, floor lookdown distance, and allowed area mask; `CaveTraversal` covers `Ground`, `Flying`, `Swimming`, `Climbing`, `Custom`. The shared provider uses deterministic integer cm and exact SDF sign convention; it checks body clearance through volume cross-sectional and vertical samples and checks intermediate points when validating links.

The `CaveNavigationCallbacks` are optional immutable epoch-aligned hooks from existing owners: `submerged(point)` (NEREID), `climbable(a,b)`, `traversalCost(point,traversal)` (PHYSIS/terrain/cost), `areaAt(point)`, and `customLink(a,b,profile)`. Custom links may reject transitions but cannot bypass occupancy or intermediate solid wall checks. No new water/weather/hazard simulation or second genre-specific state store is created. Note that the sampler is conservative and bounded, not an exact analytical capsule sweep; authoritative physical collision still belongs to Box3D/SpatialWorld and must be verified independently.

### 2. Algorithm-neutral deterministic 3D graph

`CaveNavigationGridView(sharedProvider,CaveGraphGrid,CaveAgentProfile)` supports a bounded 3D signed-centimetre region up to 1,048,576 cells (default max 65,536). It offers stable integer cell IDs, `coordinates`, `centre`, `sample`, `passable`, `neighbors(node,vector<CaveGraphEdge>&)` in a deterministic six-axis ±X/±Y/±Z order, integer directed costs, and `portals(clusterWidth)` for HPA/HPA-hybrid/FlowField cross-cluster graph metadata. No A*, Dijkstra, HPA or flow-field search code is duplicated here. Solvers own open sets, tie-breaking, path relaxation and caches.

`CavePathRouter` registers a named external solver callback with `CavePathBackend`: `AStar`, `HPAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom`. `query(name,CavePathQuery,CaveNavigationPublication)` supplies the committed provider and accepts a result only with the exact matching sourceRevision; stale queries or pending notification delivery are refused. This makes custom navigation possible without inheriting from NavMesh or changing CaveGeometry.

### 3. Native nav::Source adapter and cache invalidation

`CaveSharedNavSource(sharedProvider,CaveNavConfig,CaveAgentProfile)` implements the **existing** `engine/nav/NavMesh.h::Source` `spans` and `link` interface by sampling multiple walkable underground levels and validating profile-based transitions with the common provider. Suitable for ground/climbing path modes; flight/swimming use the 3D cell graph. Existing Stage29 authored-native NavMesh remains in place for old consumers.

`CaveNavigationPublication` subscribes callbacks by algorithm kind (`AStar`, `HierarchicalAStar`, `FlowField`, `NavMesh`, `Hybrid`, `Custom`). `publish(newFrozenIndex,halfOpenBounds)` makes a newly committed SDF revision visible and dispatches invalidation; `invalidateResidency(halfOpenBounds)` dispatches an additional topology invalidation when streaming loaded chunks change at the SAME SDF revision. A failing consumer callback retains an undelivered event for `dispatchPending()`, and path routing is blocked until those notifications catch up. Subscribers should not mutate the registry recursively during notifications.

`caveGraphDirtyRange(bounds,grid,halo)` maps authored signed cm bounds into inclusive dirty 3D grid coordinates; `caveHpaDirtyClusters` maps into 3D portal/cluster dirty ranges; `markCaveNativeNavDirty(mesh,navConfig,bounds,halo)` marks existing `nav::Mesh` 2D tile-columns (including all relevant stacked levels). The consuming solver decides its own incremental recomputation.

`CaveGameplayRuntimeFrame` now owns a `CaveNavigationPublication` and `CavePathRouter`. It records/merges pending SDF dirty bounds on `onEditedVolume` or successful `onAuthoringOperation`; **only after** `publishWithExclusiveWorld` confirms true Box3D/SpatialWorld/NavMesh/GPU commit does it publish the new immutable index to navigation consumers. Streaming build/retire native ACK likewise emits topology invalidations. Runtime API:
- `subscribeNavigation(algorithm,callback)`, `unsubscribeNavigation(ticket)`
- `publishedNavigation()`, `navigationInvalidationsPending()`, `retryNavigationInvalidations()`
- `registerPathSolver(name,backend,solver)`, `unregisterPathSolver(name)`, `findPath(name,query)`
- `setNavigationChunkDimensions(resolution,spacingCm)`

## Important limits and remaining work

**Critical distinction:** `engine/game/MapGrid.h` existing `findPath`, `astarCells`, `hybridPath`, `flowAcquire` and `FlowField.cpp` / `MapHpa.cpp` currently depend on the 2D MapGrid layout and path-layer state. Stage41 supplies a 3D navigation graph and a pluggable path router, **but does NOT refactor or natively wire all those exact 2D routines** to arbitrary 3D graph callbacks yet. Before stating A*/HPA*/Flow/Hybrid are automatically operational for free-flying multi-level cave routes, create a shared engine solver input abstraction / adapters and connect them without duplicating solver algorithms in Caverns. The native NavMesh projection is directly implemented but still requires host instantiation/build/refresh. The user's custom algorithm can already be registered as a callback using the general source interface.

- The ordinary GameClient Stage40 cave rendering is **visual only**. True gameplay physics/NavMesh and the per-algorithm invalidation bus are in the Stage34–35 `CaveGameplayRuntimeFrame`, which still requires a real authority owner and native Box3D/SpatialWorld/NavMesh barriers. No gameplay dirty edits are acknowledged from visual preview.
- Swimming/climbing/hazard/path costs require real versioned NEREID/PHYSIS/ECS callbacks supplied by host; Stage41 does not silently create fake water or climb surfaces.
- SDF samples use a conservative discrete clearance stencil and stepping; diagonal corner motion and exact swept collision require later solver/physics agreement. 3D graph is six-connected; if a solver needs diagonal edges, it must enforce no corner cutting with shared clearance, not guess a direct shortcut.
- The registry may have multiple named custom solvers. A registered callback is only an interface; `CavePathBackend::AStar` does not imply an engine A* was automatically connected.
- `CaveNavigationPublication` delivers owner-thread notifications and can retain pending events. The caller must not run path queries against stale derived caches while notification delivery is pending.
- Keep navigation publication aligned with streaming residency and authored SDF revision. Do not persist/hashes derived portal/flow/navmesh caches as ECS authority.
- All edits are in the feature branch, with skill and AGENTS ownership documentation. Do not merge main before native feature QA.

## Actual verification

User requested **features first, then tests**. Stage41 ran NO compiler, CMake/CTest, native gameplay, headless 3D pathfinding, NavMesh, Box3D, GPU, SDF clearance parity or custom solver runtime test. GitHub connector acknowledged the source/docs commits above, which verifies publication only, not successful compilation or correctness.

## Next concrete stage

1. Refactor or adapt shared engine grid A*, HPA*, FlowField, Hybrid solvers to consume `CaveNavigationGridView` without cloning their search algorithms; integrate agent movement in the real ECS owner, including flying/swimming/wall traversal and NEREID/PHYSIS area costs.
2. Wire this provider + notification bus to the actual native gameplay scene owner under exclusive Box3D/SpatialWorld/NavMesh publication. Ensure solver re-planning is triggered by authored Carve/Fill and streaming retire/reload after successful world commit.
3. Finish remaining GPU/STRATA lifetime/large-shard features and world recovery, then run full C++ build and comprehensive multilevel nav/physics verification when user authorizes test phase.
4. Continue independent Stage42 handoff plus skill update, preserving genre neutrality and features-first order.
