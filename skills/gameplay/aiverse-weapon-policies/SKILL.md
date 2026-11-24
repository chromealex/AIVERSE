---
name: aiverse-weapon-policies
description: Reuse AIVERSE seeded spread, recoil, swept projectiles, material penetration, radial explosions, ammo feeds/overheat, charge modes, attachment stat stacking and ballistic aim/lead solving when building native C++ games across genres.
---

# weapon-policies

## Mandatory composable ECS weapon architecture

Follow `AGENTS.md` and `docs/ecs-mechanic-composition.md`.
Weapons must be built from reusable ECS components and systems, not separate
RTS/FPS projectile calculations. The public integration reference is
`"engine/mechanics/EcsProjectileSystems.h"`, which binds a shared
`ProjectileFlightComponent` and `BallisticFlightPolicy` into any
`Simulation` and delivers ordered `ProjectileImpactEvent` records.

A weapon feature must separate:
- `WeaponState` / `WeaponLaunchSystem` (fire mode, validated ammo, cooldown),
- `ProjectileFlightComponent` / `ProjectileFlightSystem` (trajectory; pluggable
  ballistic/homing/rocket guidance policy; existing `stepProjectile` for gravity),
- `Collision/ImpactSystem` and `DamagePayload/ApplyDamageSystem`
  (typed hits, armor, friendly-fire, resistance and splash),
- `Despawn` and presentation systems (deferred destruction, particles, sound).

RTS artillery, FPS bullets, turret-on-vehicle, homing rockets and spells
are different *configurations and adapters* of the same underlying systems.
No `GameCore&`/`fps::Module&` dependency in the reusable system.
Do not keep a second authoritative copy of weapon, projectile or damage state.
Use Fixed math and stable Entity IDs; collect hits first, apply damage and
RNG in deterministic order at the commit barrier. Never mutate World topology
during component iteration.

**Proof before claiming reuse:** compile/run the same public header and system
in two independent GameModules plus a combined vehicle/weapon example;
verify save/rollback/hash parity, collisions, team ownership and native C++
linkage. See `tests/ecs_projectile_composition_test.cpp`. That test shows
ballistic flight reuse, NOT a completed full FPS/RTS shooting-system migration.

1. Read `docs/mechanics-packages.md` (section Weapons and ballistics) and the catalog entry. Inspect `genre/Weapons.h` (recoil, spread, projectiles), `genre/Ballistics.h` (traceLayers/resolvePenetration, resolveExplosion, solveLaunch/solveIntercept), `genre/WeaponMods.h` (stepFeed, stepCharge, equipAttachment/resolveStats), `genre/WeaponData.h` and `genre/WeaponPersistence.h` for exact behavior.
2. One owner per state: if the project's host already owns this state (for example a host in `engine/game` or `engine/actor` that has its own movement, ammo, damage or victory), adapt that owner to these policies instead of running a second copy; never maintain duplicate health, inventory or cooldown state. A host that cannot use the policy yet is debt in `docs/no-genre-split-audit.md`, not a genre rule.
3. Load scalar JSON through generated readGameplaySettings, then genre/Configured.h adapters. Read narrative definitions with readStory; use stable localization/fact/choice IDs. Keep definitions immutable for a running save.
4. Host executables link AIVERSE::mechanics. Registered native OBJECT modules call aiverse_native_use_mechanics(aiverse_native_<id>) to propagate collision dependencies without a circular game link. Follow examples/native/libraries/GenreConsumer.cpp.
5. Route external actions as Commands, validate ownership/resources/targets, and consume policy outcomes in the authoritative owner. Presentation only reads committed state. Use sim.rng for spread, integer ticks and Fixed arithmetic.
6. Weapons specifics: call stepFeed/stepCharge exactly once per weapon per tick in the authoritative handler; `draw(n)` must atomically take rounds from the real reserve owner (do not keep a second copy of FPS module ammo). Explosion `exposure` is the owner's real LOS/cover query; apply returned damage/impulses through the existing damage owner, in the returned order. Material tables, frames and attachments come from `readWeaponDefinitions`; scalar configs from `penetrationConfig`/`explosionConfig`/`feedConfig`/`chargeConfig`/`aimConfig` plus `weaponSettingsErrors`. Fold committed outcomes into the checksum with `hashWeapon`.
7. Persist state with genre::save/load and storeBytes/loadBytes in a separate NativeState key. Bind configuration/definition versions in the save envelope. Do not put mutable state on a NativeModule instance or keep future state after rollback.
8. Run catalog/generator checks, tests/libraries/run.py, native_consumer.py and aiverse_library_genre_tests. Add real project replay/collision/UI checks for integration changes. Report policies separately from fully wired genre gameplay and list platform gaps. A solver/feed test is not evidence of a playable weapon: verify the integration with real commands, collision and replay/restore.

Build selection: `AIVERSE::lib_weapon_policies`; native OBJECT modules call
`aiverse_native_use_libraries(target weapon-policies)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
