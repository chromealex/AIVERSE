---
name: aiverse-ecs-projectile-systems
description: Compose the same deterministic ECS projectile flight and impact pipeline in FPS, RTS, racing, RPG, vehicles or native projects. Genre-neutral components, systems, collision provider, ballistics policy, ordered events and rollback tests.
---

# ECS projectile flight and impact systems

Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
`docs/mechanics-packages.md`, and
`skills/gameplay/aiverse-weapon-policies/SKILL.md`.

1. Include `"engine/mechanics/EcsProjectileSystems.h"`. Use public
   `ProjectileFlightComponent` (`ProjectileConfig` + `ProjectileState`
   + generation-safe `owner` + stable `shotId`) on project Entities.
2. Register `addProjectileFlightSystem(sim,name,collisionProvider,onImpact)`
   from any GameModule. The collision provider returns a read-only
   `world::SpatialWorld`; no `GameCore` / `fps::Module` dependency is
   allowed in the shared projectile stage.
3. The default `BallisticFlightPolicy` calls the existing
   `genre::stepProjectile`: fixed-point gravity, radius, lifetime and
   ricochet. A custom policy may add guided/rocket behavior, but it must
   return `ProjectileStep` and keep deterministic motion. Do not duplicate
   trajectory or contact math in genre adapters.
4. `ProjectileImpactEvent` is collected first, then delivered in canonical
   component dense order. Apply damage, status, explosions, material
   penetration and presentation events in separate ECS feature systems.
   Do not mutate the same projectile pool or allocate an Entity during the
   dense iteration; destruction is deferred by the flight system.
5. Keep launch/ammo/cooldown, flight, impact, damage and presentation as
   independent capability components and systems. Combine this flight with
   vehicle-mounted turrets, strategy artillery, shooter rifles, rockets or
   spells by providing settings and supporting components.
6. Verify native CMake linkage with `AIVERSE::lib_ecs_projectile_systems`
   (includes `AIVERSE::lib_weapon_policies` and `aiverse_world`), replay
   hash/rollback, old save formats, impact ordering and cross-genre reuse.
   `tests/ecs_projectile_composition_test.cpp` is the reference fixture.
7. This flight skill is **one part** of the complete common shooting pipeline.
   For weapon inputs/ammo/cooldown and deterministic deferred projectile creation,
   use `skills/gameplay/aiverse-ecs-weapon-launch/SKILL.md` and
   `"engine/mechanics/EcsWeaponLaunchSystems.h"`. For health, impacts and
   ordered damage, use `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`
   and `"engine/mechanics/EcsDamageSystems.h"`.
8. Run `ecs_projectile_composition_test` (flight-only),
   `ecs_cross_genre_projectile_test` (flight + health) and
   `ecs_cross_genre_weapon_pipeline_test` (full launch-to-damage)
   before considering genre-neutral mechanics reusable.
9. These tests compose the shared pipeline in independent host games; they
   do **not** imply the legacy RTS/FPS fire/damage stacks have been migrated.
   Document remaining adapters and native test status honestly.

## Physical versus kinematic projectiles

The public `EcsProjectileSystems.h` now also exports
`"engine/mechanics/EcsSweepQuerySystems.h"`. Use
`selectSweptProjectileContact(candidates,origin,displacement,extent,bounds,preferTie)`
for dynamic-target collision of a moving projectile, with expanded AABB and
fixed-point nearest contact. A swept endpoint fraction of 1 is a valid hit;
tie policy is explicit (for example, smallest semantic Entity ID). Reuse this
primitive rather than reimplementing capsule/swept-box targeting in FPS,
RTS artillery, ships or ability systems.

The **real FPS programmed-projectile** update in
`engine/actor/ActorProjectiles.cpp` now calls this shared sweep selector. It
retains its existing terrain-wall blocking, penetration, on-hit effects and
spawn sequencing. The programmed projectile's core trajectory and damage
are still part of the compatibility code; do not claim full ownership
migration until save/rollback parity tests prove it.

For RTS shots that only need an abstract arrival or homing motion, use
`AIVERSE::lib_ecs_kinematic_projectiles` and
`EcsKinematicProjectileSystems.h` instead: no physical collision
world is required. For instant FPS or RTS beam hits, use the independent
`AIVERSE::lib_ecs_hitscan_resolution` system. All paths can share
impact/damage policies; **do not choose by genre name**.

The `ecs_projectile_composition_test` fixture now also exercises
the shared sweep selector with tie-breaking, expanded extents,
endpoint hits and no-hit cases. Actual native result remains unverified.

## Simple bouncing projectiles (physical, timed effects)

For grenades, thrown objects, physical mines and other projectiles whose fuse
and damage rules are managed elsewhere, reuse
`BounceMotionConfig` + `stepBouncingProjectile(position,velocity,collision,dt,config)`
in `EcsProjectileSystems.h`. The policy takes a read-only
`world::SpatialWorld`, applies fixed-point gravity before sweeping a
configurable box and handles reflection/contact inset/velocity divisor.
It **does not** expire or damage the entity. Its default (gravity=10,
half-extent=0.1, contact fraction inset=0.01, divisor=2) reproduces the
pre-ECS FPS grenade bounce arithmetic exactly.

The live FPS ordinary projectile stage is registered as
`actor.ordinaryProjectiles`, after `actor.projectilePrograms` and before `fps`
(round / character tick). It advances the `Projectile` components of the
projectile entities in ascending `id` order and emits the original immediate
hit effects; the cross-genre `ecs_projectile_composition_test` compares
bouncing physics against the exact former FPS formula on multiple contacts.

This policy is **physical collision**. RTS hitscan or kinematic missiles do not
need `SpatialWorld`; use the matching nonphysical system. Do not combine
the two into one unconditional genre-based simulation.

When implementing another mechanic, repeat the same decomposition:
**components + reusable systems + pure policies + thin project adapters**.


## Built-in FPS projectile ownership

The FPS host is ECS-only (`engine/actor/ActorComponents.h`): every projectile,
loot item and vehicle is an entity with a `Projectile` / `Loot` / `Vehicle`
component; there is no `State::projectiles` vector, mirror stage or
`actor.ecsAuthority` switch. Deterministic order is ascending `id`
(`fps::State::nextObject`), read through `Module::projectileEntities` or the
`Module::projectiles` copies.

* `actor.projectilePrograms` (`Module::stepProgrammedProjectile` in
  `engine/actor/ActorProjectiles.cpp`) composes the projectile modifier policy
  with `selectSweptProjectileContact`; no second physical solver. Collision,
  damage, on-hit / loot side effects and branching happen synchronously while
  the source projectile is stepped; entity destroys / spawns happen after the
  ordered loop.
* Weapon bullets and `Action::Frag` / `Smoke` / `Flash` throwables go through
  the private `Module::spawnProjectile` (bounded by `kMaxProjectiles`); the
  bomb-round reset `Module::clearProjectiles` destroys projectile entities in
  `id` order.
* New weapon / missile / throwable producers call the spawn adapter (or a
  future shared `ProjectileSpawnQueue` integration after order parity). Keep
  impact callbacks synchronous where the game uses same-tick damage; do not
  turn the independently queued impact resolver on for built-in FPS without
  proving identical same-tick effects, and do not conflate nonphysical RTS
  kinematics with FPS physical grenade motion.

Verify with the FPS checksum / save / rollback tests in `aiverse_fps_tests`;
do not claim passes until they are compiled and run natively.
