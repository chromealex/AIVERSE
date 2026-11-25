# Stage 15 handoff — Per-agent circulation actions and safe rider carry

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Architecture and source

- libraries/include/aiverselib/systems/BuildingRouteExecutor.h:
  genre-independent per-actor state machine. A route has Walk,
  TraverseStairs, CallLift, WaitLift, BoardLift, SelectDestination,
  RidingLift, DisembarkLift phases. Only actual movement/boarding/destination
  acknowledgement advances the state. It does NOT teleport actors or
  directly call renderer, ECS or PHYSIS.
- libraries/include/aiverselib/systems/BuildingCirculationRuntime.h:
  unchanged integer-timed lift state, validated snapshots, passenger count
  and platform motion. Occupied lifts cannot be hijacked by external pickup
  calls; only riders may request in-cabin destination.
- engine/buildings/BuildingCirculationHost.h:
  per-actor API createRouteExecutor, nextAgentCommand, pumpAgent,
  acknowledgeAgentBoarded/Disembarked. Actor-aware overloads register
  existing ECS actor IDs, reject double boarding and publish
  BuildingRiderDisplacement (old/new mm) for real passengers on moving lifts.
  Older count-based helpers are kept for compatibility with scripted cargo.
- engine/buildings/BuildingCirculationScene.h:
  dynamic SpatialWorld collider publication must complete before rider
  movement is exposed in publishedRiders. Partial failure reports only
  confirmed publishedPlatforms/publishedRiders, faults future updates and
  forces native nav::Mesh dirty/rebuild.
- engine/buildings/BuildingCirculationScene.h, BuildingLiftColliderOwner.h:
  native collision owner remains the same world::SpatialWorld, no second
  rigid-body store. Nav mesh remains the same engine/nav/NavMesh.h and
  uses revision/dirty gates for all routes.
- engine/buildings/BuildingRuntime.h:
  native include facade. Dedicated server can use the portable pure
  runtime/route headers without rendering.
- docs/agent-handoffs/building-nav-physics-2026-11-23.md:
  Stage-14 handoff documenting prior owner integration.
- docs/building-functional-recipes.md and
  examples/world/runtime-building/README.md:
  updated API workflow.

## Invariants / implementation limits

1. Movement and collision changes happen on the authoritative owner phase.
   Gameplay actors are referenced only by caller-assigned existing IDs.
2. An agent must acknowledge actual stair traversal and physically valid
   boarding/disembark positions. A graph edge alone is not locomotion.
3. Moving actors receive deltas only after a successful dynamic platform
   collider update; stale motion revisions are rejected.
4. The game owns nav::Source and must refresh dirty nav::Mesh tiles before
   resolving or querying any route.
5. A separate CPU model exists for a dynamic lift platform; it must be
   uploaded once and moved via instance transforms, never rebuilt per tick.
6. No actual ECS actor mover, passenger collision check, passenger attachment,
   articulated door meshes or full network replication loop has been wired
   into a running game scene yet. This work is owner-facing API, not proof of
   an end-to-end playable elevator.

## Commits this stage

Initial route executor: 9841137f4c47707d17424bfdff26c0fe7a821ed1
Host route executor API: 223e138ae8da73f61fd6be0abf6209468f94f123
Scene native publisher: 2689c3a5e5595e99f12cac12df4223bc710a1334
Actor rider registry: 042a3030f99497c5a0f7d018ec03e66c671908fb
Safe published rider deltas: 7e5f6bb692e449c6bd496e47c8b589a4404fa5f9
Stage 14 handoff: 103b20b3bdfc8fdbb8c503736e064ad77573d642

## Actual tests and next step

User explicitly said "тесты потом". Therefore no compiler, CTest,
Studio typecheck, native Metal rendering or live gameplay ECS smoke test
has been run for Stage 14/15. GitHub commits only prove source was pushed.
Do NOT report the feature as production verified. Historic Mac screenshot
and 19/20 legacy building QA do not validate actor navigation.

Next implementation should add an actual ECS scene spawn/actor movement
example on the existing game component APIs, safety sensors for boarding
and moving-door collision, actor attachment synchronization, and state
replication. After user authorizes tests, run and fix all focused C++/TS/
native GPU and regression suites; update this handoff with real results.
