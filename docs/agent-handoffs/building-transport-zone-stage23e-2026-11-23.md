# AIVERSE Building Generator — Stage 23E transport and zone graph adapters

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and inspected existing APIs

Continue user-directed support for all applicable graphs, not NavMesh only. Inspected actual current feature-branch source:
- `engine/game/MapGrid.h`: `game::RoadGraph` has ECS Entity `nodes`, `adj` of uint16 node indices, and `path(World&,int,int)`.
- `engine/game/MapGrid.cpp`: existing RoadGraph path uses real ECS Transform positions.
- `libraries/include/aiverselib/strategy/ZoneGraph.h`: existing ZoneGraph has directed ZoneDoor edges with permanent ability requirements; moveZone checks actor-owned ZoneState and strictly increasing sequence IDs.
- `engine/buildings/BuildingGraphChanges.h`: existing router allows one registered callback per domain; it requires explicit acknowledgment and retries.
- `engine/buildings/BuildingGraphOwnerAdapters.h`: existing MapGrid cache and room graph adapters.
- The rail implementation is discoverable in the default branch as `libraries/include/aiverselib/mechanics/Railways.h`, but that path was not accessible on this feature branch, so avoid introducing a mandatory nonexistent include.

## Source changes

NEW `engine/buildings/BuildingTransportZoneGraphAdapters.h`:
- `BuildingRoadGraphView`: `initialize(const game::RoadGraph&, CanTraverse, error)`, `rebuild(revision,error)`, `consumer()`, `graph()`. Makes a derived, filtered adjacency view preserving original ECS node ordering, with no fabricated roads or pathfinding algorithm. Game's authoritative predicate decides whether each road edge is traversable. Queries still use existing `RoadGraph::path(World&,from,to)` against the derived graph.
- `BuildingZoneGraphView`: `initialize(const lib::strategy::ZoneGraph&,CanTraverse,error)`, `rebuild`, `consumer`, `graph`. Reuses original directed zone definitions, filters only unavailable existing directed doors, retains required-ability constraints; does not mutate ZoneState, add reverse door edges, or change checkpoints.
- `buildingRailOwnerInvalidator`: optional dependency-free callback bridge for the real railway network owner to accept/reject access changes after it checks active reservations, route/block ownership and train safety. No pretend RailNetwork implementation on this branch.
- `buildingAllGraphOwners`: fanout for multiple independent consumers sharing a single graph domain, e.g. road and rail under Transport. Returns false if any refuses; consumers must be idempotent across retries.
- `engine/buildings/BuildingRuntime.h`: includes the new adapter.
- `skills/world/aiverse-building-style/SKILL.md`: Stage 23E contract documented, existing catalog registration reused.

## Exact integration

After physical collider changes are committed by BuildingApertureOwner and before graph/router ACK:
1. Refresh game-owned road access predicate from actual world state; register `roadView.consumer()` as Transport when road edges are affected.
2. Refresh game-owned zone access predicate from actual world state; register `zoneView.consumer()` as Gameplay when directed zone doors are affected.
3. For rail-dependent gates, bind `buildingRailOwnerInvalidator(gameRailCallback)` to an actual rail owner; combine with road using `buildingAllGraphOwners({roadView.consumer(), railCallback})` if both share Transport. Rail callbacks must explicitly reject a closure while an active train occupies the affected block, or stage the change safely through the existing game authority. The adapter alone does not stop a train.
4. Router receives a strictly revisioned committed-state change and makes each adapter rebuild a derived snapshot or enqueue its own owner update. Retry refused/unregistered domains without mistaking graph ACK for completion of all simulation work.
5. Game code routes only through the validated current graph view. Ability-gated ZoneState mutations, actual road vehicle motor movement and rail block state still belong to the existing game systems.

## Limitations and test evidence

This is an adapter stage, not integrated gameplay. In particular: original RoadGraph pointers must outlive views; changing node topology requires owner-triggered rebuild; ZoneGraph directed policies and existing ZoneState may require a separate persistence/version migration; railway safety is not automatically verified; arbitrary road crossings cannot be inferred from building geometry. The Stage 23E route does not implement runtime traffic simulation or discover new road links.

User explicitly deferred tests. NO Clang/GCC compile, CTest, PHYSIS/nav/game scene execution, Mac GPU screenshot, Studio TS, network or train simulation tests were executed. Source commits are NOT proof of compile/runtime correctness. Keep PR 114 Draft; feature branch remains divergent from main.

## Commits
- `940b4c49ac450295ea7814423b323e120f0c8101` — road/zone/rail adapters.
- `a19c69512e539ee6b2766940b71939c4b31af1c0` — runtime export.
- `3174eb6834fead112e77e98c3d9891f554b34cbf` — composite owner callback.
- `f4c372e6adec3e0fa876dacae9f9f5c491040c74` — registered building skill update.
- This handoff is its own subsequent commit.

## Next concrete step

Stage 23F: integrate authoritative game fixed tick and doorway command dispatch with actual actor access checks, graph-source refresh and snapshot/render dependency publication. Correct cross-domain revision/retry semantics (including callbacks that can throw), optional rail branch reconciliation and renderer leaf animation. Only after all feature stages, run full native build and tests and reconcile main without overwriting concurrent changes; write a handoff after each stage.
