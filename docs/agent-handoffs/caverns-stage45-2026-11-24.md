# Caverns Stage45 — architecture correction: genre-neutral ECS steering and collision owner

Date: 2026-11-24

## Objective

User review flagged that Stage44 introduced `actor::ActorHost`, `CharacterBody`, `actor::Input` and `CaveActorMotorSteering` into universal Caverns/navigation workflows. This conflicts with AIVERSE's ECS and reusable library requirements. Stage45 makes an intentional architectural correction BEFORE expanding features.

## Repository, branch, commits

Repository `chromealex/AIVERSE`; branch `feature/terraweave-caverns-core-20261123`; no `main` merge.

Actual GitHub source operations:
- `d0797f72b7c1f6c7f84d976b83cc4282f3b85e39`, `8aaae87c23f4f016d3868363617ecbbe6d6f8b1a`: restore `engine/actor/ActorModule.cpp` and `engine/actor/ActorModule.h` EXACTLY to Stage43 pre-ActorHost versions (commit `04ef13be23d386013eb577048aef1d9365993937`). This removes Stage44 cave-specific actor initialization, collision insertion, routes, custom resolvers and control overrides. Existing game-specific actor code remains unchanged.
- `45afc4017858cc5dbeec8e56396728113a4d5510`: delete `engine/nav/CaveActorMotorSteering.h`.
- `889ba18a5e56ca7ce22c900ee75f378ed6936264`, `09f811999a679aada7bd9b5ec0b1fc15968abf20`: delete incomplete ActorHost-specific Stage45 collision commit/catalog headers.
- `a3987b2fb3b9928a53cd08be2bb352103daaf0e3`: new `engine/world/TerraWeaveCavernsSpatialRevisionOwner.h`, bounded generic `SpatialWorld` staged-revision source.
- `1189609750680e0bd3bc37c0702570b62556cd3c`: new `engine/nav/CaveEcsSteering.h`, generic per-entity movement intent producer.
- `c9c4f2c717e07fe5c2954e0a03376e4d22dbd619`: add `nav::installCaveRepathSimulation`, persistence/route refresh without a second mover.
- `a5176318c5619998a173435c785bea40105b9c81`: mark historical Stage44 handoff superseded.
- `7d43b0e8367695362f0be8358c1f1aff6f2abdcb`, `e05b846562df74eb81545042efb055061cc64973`, `5fd9cc82ca211439b900bb23327fdd05ab7f0d80`: superseding updates to Caverns skill, Pathfinding skill and `AGENTS.md`.
- `b343b833bf20d368baa4b92b0a36adb6b61e22f1`: `docs/pathfinding.md` clarifies retirement of ActorHost-specific integration.
- This handoff is the subsequent Stage45 documentation commit; obtain current HEAD for exact tip.

## Correct native architecture

`TerraWeaveCavernsSdfSpatialIndex.h` is the authoritative immutable SDF data input. `TerraWeaveCavernsNavigationData.h` and `TerraWeaveCavernsNavigationGraph.h` expose passability, costs, adjacency, dirty bounds and portals to shared `engine/nav` solvers (A*/HPA*/flow/hybrid/native NavMesh/custom callback). There are NO imports of `engine/actor` in these paths.

The game/world's existing movement motor remains the single position writer. Route assignment uses existing ECS `CaveRouteIntent` and `CaveRouteFollower`, independent of a particular genre. An authority can choose one of two non-overlapping modes:

1. **Motor-neutral** (recommended): install `nav::installCaveRepathSimulation(sim,hooks)` to get authoritative fixed-tick route persistence/replan and rollback. Then `nav::installCaveSteeringSystem(sim,source,positionReader,maxEntities)` or call `prepareCaveSteering(world,publishedData,positionReader)` before the existing motor tick; it populates `CaveSteeringRequest` on entities that opt in. Each project controller reads that request in its own movement system. Walking, flying, swimming, climbing, vehicles and custom controllers may interpret steering differently. This library NEVER updates position or velocity. The game host supplies position from its own ECS owner via a callback.
2. **Direct Transform follower** (legacy opt-in): use `nav::installCaveNavigationSimulation` when the project deliberately selects Stage43's generic Transform follower as its sole motor for those entities, and provides a real collision callback. Never combine this on the same entity with another motor.

`CaveSteeringRequest` is a portable persistent ECS component containing destination, integer cm direction, speed, traversal mode and the source/environment epochs. A stale SDF/environment revision invalidates steering and route rather than allowing movement. Only the real physical motor decides path following, collision/contact response, movement state, vehicle dynamics, gravity and animation.

### Collision ownership

`CaveSpatialRevisionOwner` replaces actor-specific collision classes. It is a genre-neutral class owning bounded per-chunk leases in the *existing* `world::SpatialWorld` (not a second world). Initial mesh installation uses full-resolution `prepareCaveVolumeCollision` with reserved stable collision IDs. `prepare(active,changedMeshes,nextRevision)` constructs a private complete copy of the current world and changes ONLY the leased cave triangle IDs while preserving all other colliders. `publish(active,candidate,exclusive)` swaps in that prepared copy only when an actual owner exclusion predicate remains valid and the old revision still matches. A failed preparation has no effect on live collision. This method does NOT itself publish Box3D, NavMesh or GPU sources or claim cross-owner atomicity. Their corresponding owners must coordinate via Stage35's real quiescence and full transaction barrier. If any external irreversible operation fails, stop/enter world recovery; no arbitrary callback `true` counts as proof.

### What was intentionally undone

The Stage44 source-level modifications to `engine/actor/ActorModule.h/.cpp` have been **reverted**. There is no longer cave-specific `map.cavernsNavigation` handling inside ActorHost and no concrete CharacterBody/actor::Input route steering in common nav. Historical Stage44 handoff is retained but explicitly marked superseded. The obsolete Stage45 ActorCollision files were deleted.

### Remaining real work

- Generic ECS motor-neutral hooks must be installed in an ACTUAL game authority with the proper ECS position callbacks, native collision/physics scene, order of systems and input validation. The generic library cannot guess the controller for every project.
- Wire the generic SpatialWorld candidate owner to the existing Stage35 Box3D/NavMesh/GPU transaction and the host's real quiescence; verify handling of partial irreversible commits.
- Do not build an FPS/RTS/actor-specific pathfinding implementation or duplicate navigation algorithms.
- Stage38 STRATA sidecar retirement, large GPU page uploads, Stage40 world recovery and full server/client gameplay integration remain separate feature tasks.
- **User requested features first and tests later.** No C++ build, CMake/CTest, runtime physics, ECS, navigation, GPU or save/replay test was executed for Stage45. Connector commits prove publication ONLY; code remains unverified until native acceptance.
