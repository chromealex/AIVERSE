---
name: aiverse-ecs-hitscan
description: Share deterministic hitscan target selection across FPS/RTS and native games while preserving rewind, collisions and exact-tie ordering without simulated projectile physics.
---

# Shared ECS hitscan policies

Include `"engine/mechanics/EcsHitscanSystems.h"` and use
`AIVERSE::lib_ecs_hitscan` from a native target:
`aiverse_native_use_libraries(your_target ecs-hitscan)`.

## Compose the mechanic

1. Your input/weapon system validates an accepted shot and generates ray
   origin/delta and max distance. Do not advance a moving projectile for
   instant hitscan weapons.
2. `selectClosestHitscanCandidate(candidates, origin, delta, maxFraction,
   eligible, bounds)` finds the closest valid actor/vehicle/other candidate.
   `eligible` implements target/life/team rules; `bounds` supplies a
   collision hitbox. The results include the original candidate pointer,
   intersection fraction and normal.
3. Keep environment/vehicle obstruction and penetration policies in the
   native adapter if they cannot be uniformly expressed by the target pool;
   compare their fractions against the current nearest candidate, preserving
   the legacy query order.
4. Dispatch hits into `EcsOrderedImpactSystems.h` and the game's damage
   policy, which may use the FPS `CharacterHealth` component, ECS RTS `Unit`, or a
   custom health/status component. Do not rewrite gear, shields, headshots,
   faction checks or on-hit event sequencing.

## Rewind and deterministic behavior

The candidate range may be an immutable historical world snapshot. This is
how actual FPS `Module::shoot` preserves lag-compensation selection; the
shared selection policy never reads wall-clock time, moves actors or consumes
RNG. Strictly nearer fractions replace the winner; exact ties preserve input
order. An invalid candidate is skipped. A world collision prior to the
candidate fraction can still block the shot.

Use the nonphysical projectile system for moving RTS missiles,
the ballistic flight system for physical projectiles, and this hitscan
selection policy for instantaneous collision resolution. These are
**weapon choices**, not FPS/RTS genre switches.

## Current implementation boundaries

Real `Module::traceShot` in `engine/actor/ActorSystems.cpp` already uses the shared
selection policy over current/rewound Actor views. Vehicle/material
collision and damage remain in the FPS adapter. This header is not yet a
separately registered `HitscanSystem` with ECS fire-request components.

Run `ecs_hitscan_selection_test` via CTest or
`tests/run_ecs_experiment_suite.sh`. Also run native FPS replay,
headshot, penetration and transport rollback tests before trusting
a game build. Never claim tests passed without executing them.
