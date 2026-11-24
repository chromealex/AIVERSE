---
name: aiverse-ecs-kinematic-projectiles
description: Use deterministic nonphysical projectile motion in RTS, FPS, tower defence, abilities and hybrid games; choose timed arrival or target pursuit without a physics dependency.
---

# ECS kinematic projectiles

Use the public native header `"engine/mechanics/EcsKinematicProjectileSystems.h"`
and package `AIVERSE::lib_ecs_kinematic_projectiles`
(from `aiverse_native_use_libraries(your_target ecs-kinematic-projectiles)`).
Do not copy these algorithms into a particular genre module.

## Compose from components and systems

- `KinematicProjectileComponent`: origin/current position, destination, source/target entity, stable shot ID, simulation-tick lifetime, motion mode, fixed-point speed for pursuit.
- `addKinematicProjectileSystem`: progresses components in stable ECS dense order, optionally resolves updated destination per simulation tick, emits `KinematicArrivalEvent` in stable **Entity-index order** at arrival (independent of sparse-set swap-removal), and defers despawn. Wire damage/area effects to a **separate ordered impact stage**; a timeout is not a hit.
- `KinematicMotionMode::TimedEndpoint`: finish at a static endpoint after `duration` ticks. No collision queries or dynamic retargeting unless you explicitly write a different motion policy.
- `KinematicMotionMode::Pursuit`: chase the latest target position with `distancePerTick` and `duration` as an expiry limit. Provide a destination resolver that reads canonical simulation entities, e.g. `[](Simulation& sim, const KinematicProjectileComponent& shot) -> Vec3`. Missing targets should use a game-defined rule, such as preserving last known position.
- `advanceKinematicPursuit(position, aim, step)` is the stateless fixed-point movement policy reused by the live RTS `sysProjectiles` for ordinary weapon and non-arc ability shots. It intentionally preserves prior `moveTowards` semantics.

## Not a genre toggle

RTS often uses nonphysical shots, but can use physical ballistics; FPS can use hitscan without flight or use either flight policy. Select a policy per weapon/entity. Never select motion by checking RTS/FPS genre.

The physical pipeline is `EcsProjectileSystems.h`. Hitscan (including FPS rewind) is another separate resolution path. Common launcher, impact and damage systems should not depend on which motion system runs.

## Migration safety

Existing RTS `Projectile` and `Transform` are ECS-backed; their non-physical pursuit path now uses the shared policy, but the legacy `sysProjectiles` still owns target resolution, collision/arc, damage and effect order. Do **not** assume `addKinematicProjectileSystem` replaces that whole stage yet. Converting all live RTS projectiles requires parity tests for moving/dead targets, splash, AoE abilities, penetration/programmed paths and rollback.

Tests: `ecs_kinematic_projectile_test` in CTest, or `tests/run_ecs_experiment_suite.sh`. Verify actual CI and native gameplay before enabling production.
