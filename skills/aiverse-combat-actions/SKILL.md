---
name: aiverse-combat-actions
description: Reuse AIVERSE combat actions (frame-keyed hitboxes/hurtboxes, dodge invulnerability, directional block and perfect parry, poise/stagger, lock-on targeting) over melee-combat when building native C++ action, fighting or soulslike games.
---

# combat-actions

Use when a native/host combat owner needs spatial melee hits, i-frames, parry/block direction, stagger or
lock-on. Attack timing/combos/hit dedupe come from `melee-combat` (`genre/Melee.h`); this package composes it.

1. Read `docs/genre-packages.md` (combat-actions rows and runtime contract) and the catalog entry. Inspect
   `libraries/include/aiverselib/genre/CombatActions.h` for exact behavior.
2. Responsibility: contacts and outcomes only. `collectMeleeHits` returns `HitContact`s (self/team filtered,
   once per swing via `recordMeleeHit`); `resolveCombatContact` returns Hit/Blocked/Parried/Evaded;
   `applyPoiseHit` returns poise damage and stun ticks. Health, damage formulas, stamina and status effects
   stay with the existing combat owner. Never keep a parallel health/status system.
3. Data: scalar knobs live in the generated `combat` section of `libraries/spec/gameplay.json` (ticks and
   milli-units). Load with `readGameplaySettings`, then `genre/CombatConfigured.h`: `dodgeConfig`,
   `guardConfig`, `poiseConfig`, `lockOnConfig`. Hitbox definitions: `genre/CombatData.h` `readHitboxes(json,
   attacks)` (example `examples/libraries/genre/hitboxes.json`); frames must lie inside the attack's active
   window; errors are readable `JsonError`s. Edit the spec and run `python3 tools/libraries/generate_gameplay.py`.
4. Per authoritative tick, in order: `stepMelee`, `stepDodge`, `stepGuard`, `stepPoise`, `stepLockOn` (each
   rejects repeated/backwards ticks), then `collectMeleeHits` with the bodies' current poses, then for each
   contact `resolveCombatContact` -> owner damage -> `applyPoiseHit`; on `Parried` call `applyParryStagger`
   on the attacker; on stagger call `interruptMelee`. Authorize callbacks check stamina/stagger atomically.
5. Shapes: Fixed metres, yaw in turns (yaw 0 faces +Z, +X right). Sphere, capsule, yaw-only rotated box
   (radius 0). Use stable positive body IDs; candidate/target IDs must be unique. Lock-on ties break by ID.
6. Persist `CombatActorState` (or the individual states) with `genre::save/load` from `genre/Persistence.h`
   (record kinds 32..36) and `storeBytes/loadBytes` in a dedicated NativeState key; feed `combatHash` /
   `hashCombatState` into the simulation checksum. Bind the settings/hitbox versions in the save envelope.
7. Example: `examples/native/libraries/GenreConsumer.cpp` (`libraryCombatConsumerExample`).
   Run `python3 tools/libraries/catalog.py check`, `generate_gameplay.py --check`, `tests/libraries/run.py`
   (genre tests `combatActions`), `native_consumer.py`. Not implied: animation-driven boxes, continuous sweep
   between ticks (fast blades may skip thin targets), ragdoll, camera lock presentation or AI usage.

Build selection: `AIVERSE::lib_combat_actions` (depends on `AIVERSE::lib_melee_combat` and `aiverse_engine`);
native OBJECT modules call `aiverse_native_use_libraries(target combat-actions)`. Only this header library
and its declared dependencies are selected. See `skills/catalog.json` for dependency routing.
