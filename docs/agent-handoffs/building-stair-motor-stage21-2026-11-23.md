# AIVERSE Building Generator — Stage 21 collision-gated stair motor

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Implemented source

- NEW `engine/buildings/BuildingStairMotor.h`: genre-independent `BuildingStairMotor<Body>`, `BuildingStairSweep`, `BuildingStairMotorReport`.
- `begin(Entity,BuildingNavRouteStep,nav::Mesh,sweep)` accepts only resolved ClimbStairs routes with genuine native navigation anchors, existing physical tread/landing waypoints, a built non-dirty NavMesh and a nonempty authorized sweep function. Both corridor endpoints must match the graph's resolved landings.
- `advance(World&,nav::Mesh)` runs in an exclusive authoritative ECS owner phase. Refuses dead entities, missing Body, stale NavMesh revision and rejected physics sweep. It advances a single stair waypoint after the game's trusted actor-shape sweep approves full displacement; never assumes an off-mesh room edge is a physical route.
- `acknowledgeLanding(BuildingRouteExecutor&)` only advances the stair route command once the final physical waypoint has been approved and the executor still references the same connection ID.
- `cancel()` drops local traversal state; no implicit teleport or ECS actor retirement.
- `engine/buildings/BuildingRuntime.h`: exposes the motor in the runtime umbrella.
- `skills/world/aiverse-building-style/SKILL.md`: Stage 21 usage, ownership, limitations and next action. The existing catalog entry remains valid.

## Architecture and integration contract

The existing `BuildingStructureGraph` creates physical StairTread/StairLanding solids. Existing `BuildingNavMeshBridge` derives stair corridor waypoints from these solids and validates native `nav::Mesh` anchor polygons. Existing `BuildingRouteExecutor` owns route phases. This stage adds only an ECS-facing movement adapter: world-owner game code provides a trusted `BuildingStairSweep` callback performing real collision/step/ground support verification through its current PHYSIS or character controller. A callback returning true without collision checks defeats the safety contract; no production example should do so.

Intended integration per actor:
1. Compute `host.route(navMesh, fromRoom, toRoom)` and create the matching route executor for room graph traversal.
2. For each `ClimbStairs` step, call `stairMotor.begin(actor, navStep, navMesh, physicalSweep)`.
3. Each authoritative fixed tick, after any relevant world collision synchronization, call `stairMotor.advance(ecs, navMesh)`. This stage moves one approved waypoint per call; the game motor controls actual animation speed and step sweeps, not this adapter.
4. Once `reachedLanding` is true, call `stairMotor.acknowledgeLanding(executor)` to move to the next graph action.
5. Dirty NavMesh, blocked sweep, actor lifetime changes or scene streaming must pause/cancel the motor and request replanning. Rendering is driven from ECS state through the existing renderer owner.

Do not add FPS/RTS special cases, a second navmesh, fake vertical portals, client-side authoritative collision, or mesh reconstruction each tick. This API does not yet implement speed-limited physical substeps, slope policy, character capsules, physics rigidbody impulses, motion interpolation or a concrete game actor controller binding. Those must be integrated with the existing motor owner in a later stage.

## Provenance

- `34f29cfab60fbf6b7f7452fa67bf5264e215fa6e` — new stair motor.
- `46931ce2bc966ace14212a716f72b83b9b043526` — runtime umbrella.
- `bb05fbe820f30e4dc2be81c1c282daeb5b6fcfb0` — skill update.
- Handoff publication is a separate commit.

## Actual validation and blockers

User explicitly deferred tests. No C++ compilation, PHYSIS sweep, live in-game stair traversal, native CTest, Metal/GL33 screenshot, Studio TypeScript or multiplayer tests ran for this stage. A GitHub source commit does NOT establish build success. The actual gameplay movement/PHYSIS owner must supply and validate its collision callback. Retain Draft PR; branch diverges materially from main.

## Next concrete step

Stage 22: build native `nav::Source` from generated building solids / floor spans using existing AIVERSE NavMesh ownership, including floors, stairs and cutouts. Then connect this motor to the actual existing game actor controller with timestep-limited capsule sweeps, and test stage 19-22 lifecycle + movement together when the user lifts test deferral. Add one handoff per stage, including actual tests and provenance.
