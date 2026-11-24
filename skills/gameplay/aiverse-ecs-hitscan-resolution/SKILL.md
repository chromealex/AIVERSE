---
name: aiverse-ecs-hitscan-resolution
description: Install independent deterministic ECS hitscan fire-intent resolution in a native game, with host-provided rewind candidates, eligibility, obstruction and downstream damage handling.
---

# Schedulable ECS hitscan

Use `"engine/mechanics/EcsHitscanResolutionSystems.h"` from
`AIVERSE::lib_ecs_hitscan_resolution`, or in a native project:
`aiverse_native_use_libraries(your_target ecs-hitscan-resolution)`.

## Build a game from components and systems

The firing actor has `HitscanIntentComponent` containing `origin`,
`delta`, `maximumFraction`, a semantic `shotId` and `requested`.
Register `addHitscanResolutionSystem(sim, name, candidates, eligible,
bounds, entityOf, onHit)` after input/fire acceptance and before downstream
damage resolution.

- `candidates`: returns a stable range of actor/target poses. The host may
  return the current ECS-derived poses or a lag-compensated snapshot.
- `eligible`: excludes invalid/dead/friendly targets according to your game.
- `bounds`: returns the target's `world::Bounds` for deterministic
  `world::intersect` collision testing.
- `entityOf`: maps the selected pose to the live generation-safe ECS Entity.
- `onHit`: receives `HitscanResolvedEvent` in deterministic source entity
  dense order **after** all requested shots were traversed. Route accepted
  hits through `EcsOrderedImpactSystems.h` and a genre-independent or
  custom damage policy.

The system consumes a request exactly once even on invalid direction,
blocked fraction, no eligible targets or missing live entity; it does not
allocate any flying projectile, consume RNG, or integrate gravity.

## Choose behavior, never a genre switch

- Hitscan: instantaneous collision resolution, sometimes with FPS rewind.
- Kinematic projectile: tick-based endpoint or pursuit without rigid-body
  simulation; common for RTS missiles.
- Physical projectile: continuous flight/collision/bounce/gravity.

RTS weapons may use physical projectiles, and FPS weapons may use kinematic
missiles or hitscan. Choose per component/weapon settings rather than by
`isFPS` or `isRTS`.

## Compatibility warning

Real FPS `Module::shoot` still invokes `traceShot` in its synchronous
legacy tick to preserve interleaved player damage, lag compensation,
penetration and network checksum ordering. It already reuses the shared
nearest-candidate and ordered impact policies.

The new standalone hitscan resolver stages events *after* request traversal.
Do **not** switch the existing FPS/RTS authoritative paths wholesale to it
without parity checks on multiple simultaneous hits, deaths, armor, rollback,
rewind and replay. No default behavior has been changed by introducing it.

Test `ecs_hitscan_resolution_test` via CTest or the standalone
`tests/run_ecs_experiment_suite.sh`. It includes independent host styles,
one-shot request consumption and deep/COW snapshot parity. Execute native
builds before claiming test success.
