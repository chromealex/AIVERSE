---
name: aiverse-tag-conditions
description: Reuse AIVERSE C++ all/any/none tag filters and bounded-value conditions for native game projects instead of generating duplicate gameplay algorithms.
---

# tag-conditions

Read `docs/gameplay-systems.md`, `libraries/include/aiverselib/systems/Tags.h` and this package's catalog entry.

1. Resolve type tags using Rules.tagMask; reject unknown names with resolveTags. Keep type tags separate from status/dispel tags. Missing condition inputs fail closed. Built-in EffectDef target filters remain authoritative.
2. Prefer existing engine mechanics and data settings before custom state. Keep all new outcome-affecting data in NativeState; no mutable module members.
3. Read `Persistence.h` for reservation/sequence/pity/perception adapters. Use separate keys per subsystem and validate failed loads explicitly; never silently reset a corrupt save.
4. For scalar defaults read `docs/gameplay-settings.md`; use generated readGameplaySettings and systems/Configured.h. Do not invent engine game.json keys.
5. Follow `examples/native/libraries/SystemsConsumer.cpp`, `tests/libraries/systems_tests.cpp` and `systems_engine_tests.cpp`.
6. Run catalog/generator checks, library tests, engine integration tests and external native/export tests. Verify rollback for game-specific integration; do not mutate ECS while iterating.

Pure policies link `AIVERSE::libraries`; EngineMechanics/Persistence adapters require
`AIVERSE::gameplay_native` and the normal game host. Commands remain the only external
simulation input. Presentation previews cannot authorize mutations. Keep docs, versions,
examples and tests synchronized; describe exact coverage instead of claiming a full new engine.
