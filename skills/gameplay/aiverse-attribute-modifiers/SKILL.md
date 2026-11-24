---
name: aiverse-attribute-modifiers
description: Reuse AIVERSE C++ authoritative effective stat snapshots through existing modifiers for native game projects instead of generating duplicate gameplay algorithms.
---

# attribute-modifiers

Read `docs/gameplay-systems.md`, `libraries/include/aiverselib/systems/EngineMechanics.h` and this package's catalog entry.

1. Call effectiveStats after checking the entity. Configure existing StatMod/tech/level/status rules; never evaluate a parallel modifier stack. The snapshot returns final values, not an invented per-source breakdown. Missing units and invalid stats fail.
For weapon-based effects, author `weaponDamage` on damage/heal payloads,
optionally `weaponIndex` (default 0). `GameHost::currentWeaponDamage`
returns authored weapon damage times the same authoritative Damage stat,
or no value for a missing caster/slot. Resolve at effect execution, not
from a HUD value or a second modifier evaluator. See docs/game-format.md.

Timed buff effects and status mods support `stackingGroup` for strongest-wins add/mul composition. Use one group for alternative levels of a slow or aura, separate groups for independently combining effects. Native code can use `game::upsertTimedBuff` with the one-based group ID from `Rules::modifierGroups`; keep the authoritative Buffs component, not a parallel native state. Only a single contribution per stat/group is applied. Read the group semantics and restrictions in docs/game-format.md.

For optional final movement speed bounds use top-level rules.json `minUnitSpeed` / `maxUnitSpeed` (nonnegative, ordered). They apply after the authoritative stat pipeline for types with positive base movement speed; absent keys keep previous behavior. They do not bypass movement permissions or hard immobilization controls.

2. Prefer existing engine mechanics and data settings before custom state. Keep all new outcome-affecting data in NativeState; no mutable module members.
3. Read `Persistence.h` for reservation/sequence/pity/perception adapters. Use separate keys per subsystem and validate failed loads explicitly; never silently reset a corrupt save.
4. For scalar defaults read `docs/gameplay-settings.md`; use generated readGameplaySettings and systems/Configured.h. Do not invent engine game.json keys.
5. Follow `examples/native/libraries/SystemsConsumer.cpp`, `tests/libraries/systems_tests.cpp` and `systems_engine_tests.cpp`.
6. Run catalog/generator checks, library tests, engine integration tests and external native/export tests. Verify rollback for game-specific integration; do not mutate ECS while iterating.

Pure policies link `AIVERSE::lib_attribute_modifiers`; EngineMechanics/Persistence adapters require
`AIVERSE::lib_attribute_modifiers` and the normal game host. Commands remain the only external
simulation input. Presentation previews cannot authorize mutations. Keep docs, versions,
examples and tests synchronized; describe exact coverage instead of claiming a full new engine.

Build selection: `AIVERSE::lib_attribute_modifiers`; native OBJECT modules call
`aiverse_native_use_libraries(target attribute-modifiers)`. Only this header library and its
declared dependencies are selected. Requires the existing `aiverse_game` owner. See `skills/catalog.json` for dependency and responsibility routing.
