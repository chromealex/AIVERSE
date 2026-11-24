---
name: aiverse-ecs-ordered-impact
description: Resolve ECS projectile hits through reusable, deterministic direct/on-hit/splash ordering while preserving genre-specific armor, status, faction and damage rules.
---

# Shared ECS ordered impact policy

Use `"engine/mechanics/EcsOrderedImpactSystems.h"`, package
`AIVERSE::lib_ecs_ordered_impact`, or in a native project
`aiverse_native_use_libraries(your_target ecs-ordered-impact)`.

## Components and scheduling

The projectile itself remains a real ECS entity. Its motion system can be:
- `EcsKinematicProjectileSystems.h`: fixed-endpoint or moving-target pursuit without physics
- `EcsProjectileSystems.h`: ballistic/physical flight with world collision queries
- A separate hitscan/rewind path with no moving projectile

At impact, supply `OrderedProjectileImpact` (projectile/source/target
Entity, impact location, Fixed damage and splash radius). Call
`resolveOrderedProjectileImpact` from a deterministic ECS gameplay system.

The resolver dispatches callbacks **per hit** in this exact order:
1. Direct damage policy (including dead-target checks);
2. Staging of on-hit effects;
3. Spatial-query candidates in the query's existing deterministic order,
   omitting the primary target and applying host-supplied ally/range filters;
4. Immediate splash damage for each eligible candidate.

Do not batch direct damage for all projectiles before processing splash:
shields, native/Lua damage hooks, lifesteal and death state might depend on
the previous impact in the same tick. Keep payload execution barriers
unchanged until cross-genre/save/rollback parity tests confirm a split.

## Live integration

`GameCore::sysProjectiles` in `engine/game/Combat.cpp` uses this shared policy on
ordinary non-programmed RTS weapon projectiles while keeping its existing
`dealDamage` armor/status implementation, `gs.hash.query`, faction
filter and queued Lua payloads. RTS ability/arc/projectile-program damage
is not yet migrated. FPS damage also needs an adapter and compatibility
tests; do not claim it already shares all impact systems.

## Validation

Use `ecs_kinematic_projectile_test` (CTest and
`tests/run_ecs_experiment_suite.sh`) for callback ordering, direct-target
deduplication, no-splash behavior and COW replay fixture. Still run native
RTS combat parity and multiplayer rollback before enabling any new
authority paths. The pure policy has no `SpatialWorld` dependency.
