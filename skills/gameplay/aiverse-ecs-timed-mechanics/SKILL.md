---
name: aiverse-ecs-timed-mechanics
description: Shared ECS weapon reload, regeneration and entity expiration systems for FPS, RTS, vehicles and native games.
---

# Universal timed ECS mechanics

Read `AGENTS.md`, `docs/ecs-all-features-migration-plan.md` and
`docs/ecs-mechanic-composition.md` before integration.

## Components / ownership
- `WeaponStateComponent` holds canonical loaded ammo. Attach
  `WeaponReloadComponent` for reserve ammo, magazine capacity, reload duration
  and finish tick, plus `ReloadIntentComponent` for one-tick requests.
- `HitPointsComponent` is shared with ECS damage; attach
  `HealthRegenerationComponent` for deterministic fixed-point health recovery
  (its fractional remainder is persistent/snapshotted).
- `EnergyRegenerationComponent` owns current energy, maximum and rate.
- `TimedExpiryComponent` requests deterministic despawn at the specified tick.

## Registration / order
Include `"engine/mechanics/EcsTimedMechanics.h"` and link
`AIVERSE::lib_ecs_timed_mechanics` in a native project.

```cpp
namespace lg = aiverse::lib::genre;
lg::addWeaponReloadSystem(sim, "weapon-reload");
lg::addReloadFireGateSystem(sim, "reload-fire-gate");
lg::addWeaponLaunchSystem(sim, "weapon-launch");
lg::addProjectileSpawnSystem(sim, "projectile-spawn");
lg::addHealthRegenerationSystem(sim, "regen-health");
lg::addEnergyRegenerationSystem(sim, "regen-energy");
lg::addTimedExpirySystem(sim, "expiry");
```

Set requests from validated Input/AI/Command stages, then run
`Reload -> FireGate -> WeaponLaunch -> Spawn`. Register the collision/flight
and damage stages after spawn (see weapon skill). These are opt-in features:
a unit with no `WeaponReloadComponent` is still allowed to fire; without
`HealthRegenerationComponent`, health doesn't regenerate.

Never build a second 'FPS reload' or 'RTS turret reload' algorithm. Configure
the identical systems per entity. All mutable authoritative state remains
inside components, no authoritative per-genre mirror.

## Verification and migration
- `ecs_cross_genre_timed_mechanics_test`: two unrelated hosts, exact tick
  transitions, reserve transfer, reload fire gate, fixed-point regeneration,
  entity expiration and COW/rollback checksum.
- Also run `ecs_cross_genre_weapon_pipeline_test`,
  `ecs_cross_genre_projectile_test`, FPS/RTS parity fixtures,
  native linkage, save/load migration and editor lifecycle checks.
- IMPORTANT: adding this public library does **not** switch either built-in
  `fps::Module` or `GameCore` to it. They still hold old gameplay state.
  Migrate each path with a deliberate authoritative-state ownership transition
  and golden replay/save/network parity. Do not mark production-ready on
  static review; compile and execute CTest on target platforms.


## First built-in game integrations (2026-11-18)

The opt-in `addWeaponReloadSystem` and real `fps::Module::tick`
both call `stepWeaponReload` from `EcsTimedMechanics.h`. In FPS,
the adapter builds temporary typed values from the character's
`CharacterWeapons` component, at the original per-character tick location.
**This is shared-policy adoption, not standalone per-feature component
authority**. Preserve exact same-tick reload completion, selection and
fire conditions; verify `ecsReloadAuthorityParity` before extracting
a dedicated scheduler stage.

The built-in RTS `GameCore::sysEconomy` updates its existing ECS
`Unit.hp` and `Unit.energy` via
`stepFixedResourceRegeneration` in `FixedRegeneration.h`.
The generic ECS `EnergyRegenerationComponent` system uses that same
fixed-point function, too. The RTS unit is still the single canonical
owner and no second health/energy component should be attached until
a complete ownership migration is versioned and parity-tested.

Additional regression:
`ecs_fixed_regeneration_policy_test` (integer/fractional Fixed,
negative rates, caps, stable operation order), together with normal
RTS game replay/save and FPS cross-genre tests. Native execution is
not yet verified. See `docs/editor-unification-handoff.md` and
`docs/ecs-all-features-migration-plan.md`.
