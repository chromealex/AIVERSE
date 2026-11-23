# Stage 14 handoff — Building navigation and elevator runtime integration
Date: 2026-11-23

Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)
All new code is genre-independent. Do not merge to main without reconciling
the stacked feature base with main and parallel Studio changes.

## Delivered code and ownership

- libraries/include/aiverselib/systems/BuildingCirculationRuntime.h:
  deterministic fixed-integer-millimetre/ms lift state, immutable building
  room-connection routing, typed Walk/Stair/Lift actions. Holds no ECS actor,
  physics, rendering or navmesh owner; exposes revisioned snapshots/events.
- engine/buildings/BuildingNavMeshBridge.h:
  integrates room-graph transitions with the EXISTING engine/nav/NavMesh.h
  nav::Mesh via real polygon location and native same-floor corridor search.
  Stair/lift edges remain explicit off-mesh action steps with endpoints,
  NOT invented intra-mesh portals. Link revision must match refreshed
  navmesh revision; dirty navmesh rejects routes.
- engine/buildings/BuildingCirculationHost.h:
  game-facing scene owner for lift controllers, room planning, route
  executors, revisioned platform commands and actor rider IDs. Actor
  acknowledgements are required for movement, boarding and disembarking.
  No second actor/entity store is created: rider map stores only already
  owned game actor IDs to lift feature IDs.
- engine/buildings/BuildingLiftColliderOwner.h:
  registers and moves dynamic elevator platform AABB colliders in existing
  world::SpatialWorld; marks affected nav::Mesh tiles dirty (before/after).
  Rejects stale movement revisions and oldY mismatches.
- engine/buildings/BuildingCirculationScene.h:
  attaches game-assigned collider IDs, advances bounded owner ticks, and
  exposes ONLY successfully committed publishedPlatforms and
  publishedRiders. If any collider update fails, it marks navigation dirty,
  stops publishing that lift and faults the scene for reconciliation.
  Consumers must NOT apply raw attempted motions from tick as gameplay
  transforms. Authoritative ECS/PHYSIS/render owners apply published data.
- engine/buildings/BuildingLiftPlatformModel.h:
  optional CPU-owned render::Model for a single lift platform. It is a
  dynamic dedicated model; upload once through renderer device owner.
  Platform pose changes are instance transforms, not per-tick mesh rebuilds.
- libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h and
  BuildingFeatureGenerator.h: runtime recipe generation deliberately skips
  the initial platform in static building geometry, while Studio GLB
  export includes it for accurate offline previews.
- engine/buildings/BuildingRuntime.h: one game-native include facade.

## Phase contracts

1. Register game style bytes (packed assets), call generateRecipe(), obtain
   result.building meshes/collision and result.structureGraph.
2. Create persistent static model using RuntimeBuildingModel and separate
   dynamic lift models using BuildingLiftPlatformModel. Only renderer owner
   uploads/frees GPU geometry.
3. Attach BuildingCirculationScene to the existing SpatialWorld with fresh
   collider IDs supplied by game's collision owner. The game must own
   static room/wall/floor colliders separately.
4. Refresh nav::Mesh using an authoritative nav::Source constructed from
   actual collision, then invoke scene.rebuildNavigation(mesh). No
   implicit refresh in the render or actor phase.
5. On authoritative simulation tick, call scene.update(spatial,&navmesh,dt).
   Apply publishedPlatforms and publishedRiders to render and ECS transforms
   in the owning phase, not directly from data-generation code.
6. After a moving collider invalidates navmesh, refresh then re-resolve
   links; refuse stale route queries until refreshed. If publication
   fails, fail closed and reconcile actor/physics state before reattach.

## Current limits

The core nav::Mesh has no off-mesh-link insertion API. A stair climb or
lift ride is a separate explicit owner-executed action. Route anchors
locate real navmesh polygons but do not themselves execute PHYSIS motors.
The current game has no integrated lift-door animation, rider/door collision
volume verification or ECS actor lift component in this branch. An actual
agent's stair climb still must use collision-aware movement and confirmation.
Network snapshot replication has a validated apply helper, not a proved
full-stack multiplayer integration.

## Actual verification

The GitHub connector committed source code and inspected current APIs.
NO C++ compilation, test execution, TypeScript check or real Mac/Metal
screenshots for this new navigation/runtime path were performed in this
turn, per user's explicit request to defer tests. Do not claim a passing
runtime game or automatic agent navigation. Previous user legacy
20-case C++ generation batch was 19 PASS / 1 FAIL, unrelated to lifts.

## Next

Complete per-actor route execution, ensure passenger deltas apply only
after physics commit; add safe boarding volume / car-door interlocks,
authoritative replication of actor attachments, and integration to ECS
movement owner. Then compile/test standalone graph, runtime, native
renderer adapter, platform/SpatialWorld, navmesh and Studio types; capture
actual Mac screenshots. Write next separate handoff before proceeding.
