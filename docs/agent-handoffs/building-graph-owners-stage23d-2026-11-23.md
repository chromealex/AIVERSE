# AIVERSE — Stage 23D: real MapGrid/HPA/flow and room-graph owner adapters

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Scope and actual source review

Continues Stage 23A generic BuildingGraphChangeRouter and Stage 23C dynamic NavMesh. Verified current branch contents of engine/game/MapGrid.h and MapGrid.cpp rather than inventing API: MapGrid::cellChanged(x,y) already touches flow stamps, HPA dirty cluster, clearance and navDirty; MapGrid::invalidatePathing() marks global derived pathing; flowDropAll() drops cached flow fields. Game grid is 2D (w/h, world X/Z for flat terrain) and does not represent stacked building floors. Existing BuildingStructureGraph.h is the authoritative static room/connection topology.

## Changes

NEW engine/buildings/BuildingGraphOwnerAdapters.h:
- BuildingGridGraphAdapter::consumer(game::MapGrid&,CommittedProjection,maxTouchedCells=4096):
  returns a BuildingGraphChangeRouter callback. Its CommittedProjection predicate is REQUIRED; it must ensure the real map flags/blocked source was updated by existing game owner after physics commit. This adapter only invalidates existing derived caches. It deliberately rejects sphere-map projection. It computes a conservative world X/Z rectangle, marks each affected local cell with real grid.cellChanged, and for large footprints runs grid.invalidatePathing() and grid.flowDropAll(). No duplicate A*/HPA*/flow algorithm or duplicate ECS/physics owner was introduced. Out-of-bounds changes can be ACKed as not relevant to this flat map.
- BuildingRoomGraphCache::initialize(graph,CanTraverse,error): copies immutable room ID topology and original graph connections; checks all referenced rooms. The supplied authoritative predicate evaluates currently permitted edge traversal, potentially using doors, lift states and gameplay policies. Each update builds new adjacency deterministically and swaps after completion.
- BuildingRoomGraphCache::consumer(): callback for BuildingGraphDomain::Spatial. BuildingRoomGraphCache::reachable(from,to) uses bounded BFS over the generated room nodes; room graph topology is never overwritten.
- BuildingRuntime.h exports the above native adapters.
- skills/world/aiverse-building-style/SKILL.md updated with setup/ownership contract.

## Game owner integration order

1. Authoritative game/PHYSIS owner applies door/gate/roof state and dynamic collider changes.
2. Game grid owner projects the same change into original MapGrid obstacle/terrain data BEFORE the graph-change callback is allowed to ACK; projection callback must be idempotent across retries. Register BuildingGridGraphAdapter for Hierarchy only if the game actually uses flat MapGrid for the impacted world region.
3. Register BuildingRoomGraphCache consumer for Spatial with game-owned CanTraverse callback based on committed connection access. Do not assume binary exterior entrances are internal graph edges; do not treat a lift in motion as traversable between floors without actual controller state.
4. Existing BuildingDynamicNavSource consumer handles Navigation, snapping actual aperture colliders and dirtying native NavMesh cells. Native mesh refresh remains graph owner's separate phase.
5. Publish only the exact subset of registered/relevant domains from BuildingApertureOwner setOpen; router queues unacknowledged changes. Domain callback acknowledgment means it accepted invalidation, NOT that every graph cache has completed all expensive queries.

## Known limitations

- No direct adapters yet for RoadGraph, railway, ability-gated ZoneGraph, shader/VFX, audio or power/environment networks. Their semantics and owners are independent; do not blindly route all changes to them.
- The concrete game scene's main tick, actor destroy/logout handlers, MapGrid projection and renderer have not been wired. The new room connectivity cache is independently queryable but does not yet replace the route solver's internal edges, which still use original BuildingCirculationRuntime data.
- All routes still need physical PHYSIS collision/motor checks; room reachability alone is not permission to teleport.
- Large MapGrid footprint invalidates global pathing/flow rather than pretending it is a bounded incremental update. Flat-map 2D projection cannot express upper floors.
- The room cache is undirected and game-owned callback supplies effective access. Use existing directed ZoneGraph/gameplay paths for one-way conditional links.

## Source provenance

- 8829df99b188a8b5852f6e2e017aa8cff4086e05 — new graph owner adapters.
- 1bba4965377d112c76c22fe36212d3a0481ca2ed — runtime umbrella.
- f7d6a3b17de5c029da419eb22b0538e2edcbf13e — agent skill update.
- This handoff itself is the next dedicated documentation commit.

## Actual validation and next step

Tests explicitly deferred by user. No C++ compiler, CTest, game loop, PHYSIS, macOS graphics/GPU screenshot, Studio TypeScript or multi-agent pathing tests were run. No assertion of actual runtime integration or fully supported all-graphs was made. Branch remains heavily divergent from main, PR 114 Draft.

Stage 23E next: add concrete owner adapters for RoadGraph and ability-gated ZoneGraph where a game's existing authoritative state calls for it. Then bind the host game scene tick + renderer/snapshot publishing, improve cross-domain retry consistency and run combined verification when user allows tests. Every stage must have its own skill update and handoff recording exact commits and real evidence.
