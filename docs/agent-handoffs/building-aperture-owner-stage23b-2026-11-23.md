# AIVERSE Building Generator — Stage 23B: authoritative interactive apertures

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Scope

Implemented a source-level native owner for physical binary opening/
closing of generated personnel doors, large and small gates and sliding
roof hatches. The user also requested support for all applicable graphs,
not solely NavMesh: Stage 23A introduced the multi-domain graph router,
and this stage invokes it only after a successful physical state commit.

## Modified source / ownership

- `libraries/include/aiverselib/systems/BuildingFeatureGenerator.h`:
  added optional `includeDynamicPanels` argument to
  `generateFunctionalBuilding` (default true for Studio preview).
  When false, DoorLeaf and RoofPanel are excluded from static generated
  geometry AND collision, alongside the already optional moving lift
  platform. No change to graph sockets and their stable IDs.
- `libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h`:
  runtime `generateRecipe` requests `includeInitialLiftPlatform=false`
  and `includeDynamicPanels=false` to avoid duplicate immovable barriers.
- NEW `engine/buildings/BuildingApertureOwner.h`:
  - `attach(generated,spatial,sceneId,origin,yaw,colliderIds,obstruction)`
    registers unique game-provided collider IDs for operable sockets and
    inserts one full-width closed collider per initially closed aperture.
    It rolls back initial colliders if attachment fails.
  - `setOpen(socketId,bool,spatial,router,domains)` requires a live
    authoritative obstruction probe before closure. It changes the
    existing SpatialWorld collider first, then emits a strictly revisioned
    `BuildingGraphChange` with world AABB and exact selected domains.
  - `retryGraphPublication(router)` allows a failed graph consumer
    publication to resume without pretending the physics commit failed.
    New aperture state changes are blocked while the prior notification
    remains pending. No render animation can authorize passage.
  - `poses()` provides immutable committed poses/revisions for renderer
    ownership; `detach` retires only registered live collider IDs.
- `engine/buildings/BuildingRuntime.h` exposes the owner.
- `skills/world/aiverse-building-style/SKILL.md` documents the API,
  double-collision avoidance, graph consumer ownership and limitations.

The existing `BuildingLiftDoorOwner` still exclusively controls elevator
landing doors; no duplicate physics/network ownership was introduced.
Door/roof appearance styles and game genres do not affect functionality.

## Important integration limitations

A physical close/open is currently binary and conservative: one solid
full-aperture blocking AABB represents each closed door/gate/roof. Real
hinge/sliding leaf transformations, animation time curves, and GPU model
ownership are not yet wired. Game/transport owner must authenticate
interaction calls before invoking setOpen; this is NOT a network endpoint.

Every graph domain requires the real owner callback. The Stage 23A router
does NOT automatically rebuild MapGrid/HPA*/flow, RoadGraph, ZoneGraph,
ShaderGraph/VFX, audio or simulation graphs. Unregistered graph domains
remain pending by design. Native navmesh invalidation merely marks tiles
dirty: `BuildingNavSource` currently rasterizes the generated static
StructureGraph and is not yet a dynamic SpatialWorld-composite Source.
The game must compose current door blockers into its nav source before
refresh; otherwise do not claim a closed door blocks a nav route.

The obstruction probe must sample live actors/vehicles/cargo and not
mistake the door's own existing collider for an obstruction. Ownership
of generated static solids and build placement remains with game runtime,
not the aperture owner. No automatic recovery after unexpected external
removal of a collider. The scene cannot accept more aperture commands
until all selected graph owners acknowledge the previous one.

## Commits

- `7a436d3bebb282717a14fe0f5549987b30d97251`: generator option.
- `d18a8c64b3e28d5b9eb923c823483d86518595c0`: dynamic runtime recipe.
- `815070bd083395e2a56a59c05ec99313c8f09df9`: aperture owner.
- `3ca965a42740dfe4324ee97318b4dee777b7da6b`: optional include.
- `c7d06ca359df2ac6289987ea684d7c4b32988b19`: runtime umbrella.
- `3536a5654c29ca4f7dd3933fa56b7a63545a1f04`: skill.
- This handoff is published in its own later commit.

## Actual verification

Per user's instruction, tests remain DEFERRED. No compiler, native
CTest, Mac Metal/GL33 screenshot, PHYSIS/NavMesh live integration,
network authorization test, or Studio TypeScript check has run.
GitHub commits prove only source publication, not runtime correctness.
PR remains Draft and main has diverged from this feature branch.

## Next concrete stage

1. Build composite dynamic nav::Source over static building floor spans and
   current SpatialWorld aperture colliders; regenerate and refresh dirty
   nav tiles only AFTER collision commit.
2. Add real game-owner adapters for MapGrid/HPA*/flow caches, room/zone
   topology and only the optional gameplay-relevant remaining domains.
3. Add bounded timing, intermediate visual leaf transforms, obstruction
   rechecks and rollback-safe collider updates, retaining gameplay
   permission only after physical fully open state.
4. Wire existing network authority and editor inspector, then compile and
   run comprehensive tests when deferral ends. Create a separate handoff
   after each subsequent stage.
