---
name: aiverse-ecs-weapon-launch
description: Assemble genre-independent ECS weapon launch, deferred projectile spawn, flight, collision, damage and effects for FPS, RTS, vehicles, RPGs and native projects.
---

# Universal ECS weapon launch pipeline

Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
`skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md`, and
`skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`.

1. Include `"engine/mechanics/EcsWeaponLaunchSystems.h"`.
   Components: `WeaponParametersComponent` (Fixed muzzle speed, gravity,
   radius, shot lifetime, interval, damage), `WeaponStateComponent` (ammo,
   cooldown), `FireIntentComponent` (one-tick fire request, origin, direction)
   and singleton `ProjectileSpawnQueue`.
2. **Do not put shooting inside a genre module.** Use
   `addWeaponLaunchSystem` and `addProjectileSpawnSystem` for all weapon
   owners: players, structures, vehicles, turrets, drones, monsters or spells.
   Configure different projectile trajectories/health effects through data.
3. Register system barriers in the correct order:
   `WeaponLaunch -> ProjectileSpawn -> ProjectileFlight -> DamageResolution`.
   The launch stage collects spawn commands while iterating the weapon pool;
   actual Entity creation happens in the next dedicated system.
   Flight processes hits in canonical dense order and queues damage;
   damage resolves in its own ordered stage.
4. The native game should map input, AI decisions or triggers to
   `FireIntentComponent.requested` and set its origin/direction.
   A request is consumed once. Add magazine/reload, aim, team/friendly-fire
   filtering, spread and guided missiles as **optional components/systems**,
   not hardcoded `if (genre == FPS)` branches.
5. Keep all gameplay state in ECS and serialize it, including the spawn
   sequence counter and pending command queues. Consume RNG only at
   canonical commit stages, never from worker scheduling order.
6. Public link target: `AIVERSE::lib_ecs_weapon_launch` from the native
   project. This depends on `AIVERSE::lib_ecs_impact_damage`, the shared
   projectile flight policy and collision-world APIs.
7. Verification: `ecs_cross_genre_weapon_pipeline_test` must pass for
   shooter/artillery hosts with identical checksum, ammo, spawn ID, impact,
   hitpoints, COW snapshot and replay. Also run the individual projectile
   flight/damage tests and relevant legacy RTS/FPS tests.
8. Existing RTS/FPS combat modules **do not automatically use** these
   systems. Move only one behavior at a time, compare authoritative
   simulation/replay and published save/network formats, and retain legacy
   adapters until parity is proved. Document every migration commit.

For each new mechanic repeat:
**ECS components + independent configurable systems + reusable policies +
canonical events + native-project API + cross-genre rollback tests.**
