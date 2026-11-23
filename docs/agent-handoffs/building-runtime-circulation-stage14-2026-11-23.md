# AIVERSE Stage 14 handoff — building circulation runtime

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 — Draft

## What is implemented

- libraries/include/aiverselib/systems/BuildingCirculationRuntime.h:
  genre-neutral BFS through rooms and their horizontal/vertical graph
  links, enum route actions (walk/stairs/lift). Authoritative integer-mm
  lift platform and integer-ms door/motion state, occupancy/boarding,
  destination request and capacity 8. Rejects hijacking an occupied
  cabin through an external floor call. Revisioned snapshots may be
  applied to non-authoritative clients. Coalesced platform motion
  commands for PHYSIS ownership, never direct ECS mutation.
- engine/buildings/BuildingNavMeshBridge.h:
  resolves BuildingStructureGraph landing room surfaces to actual native
  nav::Mesh polygon IDs, checks mesh revisions. Horizontal room legs
  use native NavMesh::findPath; stair vertical links include physical
  tread waypoint chains, and lift legs use explicit actions with valid
  off-mesh endpoints. NO changes to nav::Mesh portal topology.
- engine/buildings/BuildingCirculationHost.h:
  native game owner for graph/lifts/off-mesh link registry, room and
  navmesh routes, boarding, destination calls, offline state and
  tick events/movement commands.
- engine/buildings/BuildingLiftColliderOwner.h:
  existing world::SpatialWorld dynamic collider per cabin, exact
  bounds from mm position and rotated building transform; removes
  previous AABB then publishes updated one, with rollback on failure
  and stale-revision/old-position rejection. Marks native nav::Mesh
  dirty cells for owner-controlled refresh.
- engine/buildings/BuildingLiftPlatformModel.h:
  one dedicated CPU render::Model for a movable cabin, separate from
  static building graphics, uploaded once by Renderer3D owner.
- engine/buildings/BuildingRuntime.h:
  convenience native game include for generator, graph routing,
  navmesh bridge, lift collider owner and movable platform model.
- libraries/include/aiverselib/systems/BuildingFeatureGenerator.h:
  optional includeInitialLiftPlatform flag for CPU generation;
  Studio/GLB preview includes the initial platform, but released
  runtime buildings omit it from static mesh/boxes.
- RuntimeBuildingGenerator::generateRecipe uses the dynamic-lift-safe
  generation mode, preserving the original StructureGraph for spawn.
- docs/building-runtime-circulation.md:
  APIs, authoritative tick, owner publishing, navmesh refresh and limits.

## Actual tests and evidence

Per user instruction, TESTS ARE DEFERRED. No current Clang/GCC build,
CTest, Studio TypeScript compile, native GPU screenshot or PHYSIS/NavMesh
integration test was executed in this stage. Source written to GitHub.
No claim of live playable stairs or elevator ride; game actor motor
must follow provided waypoints, enforce step sweeps, attach to the
cabin and publish collision/navmesh updates.

Previous user-generated 20-building legacy batch was 19/20 and exposed
a separate factory 16x9x11 seed3 constructive-fit failure. This is not
validation of the new circulation API.

## Ownership constraints

No additional NavMesh/PHYSIS/Studio ownership layer. Native navmesh
doesn't have native off-mesh link insertion, so a revisioned overlay
adapter binds vertical graph edges to real nav polygons and an actor
motor performs the transition. The lift state is deterministic integer
server state; only the existing world owner publishes dynamic collisions.
Renderer3D owner creates GPU models and moves instance transforms;
never update GPU during simulation or rebuild a lift Model each tick.
Game owner assigns unique collider IDs and actor attachment handles.
Do not turn an unverified semantic vertical link into a claim of
successfully navigable gameplay.

## Next stage

Wire the game ECS motor and PHYSIS actor to ClimbStairs path segments,
lift boarding and passenger platform parent, with motor sweep and
lifecycle-safe actor detach. Then implement a nav::Source that samples
building-generated collision spans and floor layers so model bounds
map to actual NavMesh rectangles. Add a physically safe lift schedule,
door interlocks and networking. Finally run all deferred tests and
native Mac screenshot QA, and reconcile the stacked PR with main.
After each change update the handoff with current SHA and test status.

## Later source integration commits in this stage

- 9841137f: portable BuildingRouteExecutor.h, a gameplay-neutral
  actor route state machine with explicit movement and boarding/exit
  acknowledgements. It never teleports actors on a graph edge.
- f4b17df and 27a6ea36: BuildingCirculationHost wrappers for
  route execution, owner-confirmed boarding/exit and route cancellation;
  the native BuildingRuntime.h umbrella includes the route executor.
- 2689c3a5: optional BuildingCirculationScene.h makes the existing
  SpatialWorld + native NavMesh + circulation host integration a
  single world-owner adapter with game-provided collider IDs.
  A failure freezes publication instead of claiming stale NavMesh.
- These changes are source-level only, with TESTS DEFERRED explicitly
  by the user. No actual gameplay actor motor, multi-actor moving platform
  parent relationship, native GUI visual QA, or confirmed CI pass is
  asserted here.

Deferred next-step verification: standalone C++20 and native headers,
the navmesh source's actual walkable upper-floor spans, dynamic collision
rollback, per-actor route acknowledgement, cabin door interlocks, and
running character traversal in an actual Mac game scene. Keep PR Draft.

## Final owner adapter exposure

- Commit 9a5a86d4: engine/buildings/BuildingRuntime.h now also includes
  BuildingCirculationScene.h for native game code.
- Scene attach() registers exactly one game-assigned dynamic collider per
  elevator and rolls back on partial failure. update() applies lift motion
  to the existing SpatialWorld owner and invalidates nav::Mesh when needed.
  rebuildNavigation() accepts only refreshed native meshes; detach()
  retires registered colliders without destroying other game entities.
- Tests remain deferred by user request. No runtime/motor/physics/graphics
  verification was performed; all code is subject to later compilation
  and integration QA.

## Additional actor-owner boundary

Recent commit 042a3030f99497c5a0f7d018ec03e66c671908fb
introduced per-actor rider identity bookkeeping in
engine/buildings/BuildingCirculationHost.h. It adds
boardRider, exitRider, isRider and actor-aware route acknowledgements,
plus BuildingRiderDisplacement commands alongside cabin motions in the
circulation tick. IDs belong to the existing game/ECS actor owner; this
class neither creates new game entities nor directly updates actors.
The authoritative game owner must apply rider motion only after
dynamic cabin collision publication succeeds. No actual ECS motor or
network simulation was exercised. Tests are explicitly deferred.
