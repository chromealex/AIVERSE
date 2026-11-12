---
name: aiverse-action-sequences
description: Reuse AIVERSE C++ tick-based multi-stage actions with timeout and cancellation cleanup for native game projects instead of generating duplicate gameplay algorithms.
---

# action-sequences

Read `docs/gameplay-systems.md`, `libraries/include/aiverselib/systems/Sequence.h` and this package's catalog entry.

1. Advance once per increasing tick. begin false must have no side effects; poll is read-only; cleanup is called once for each entered stage on completion/cancel/failure. One stage transition per call bounds work. Implement navigation through existing commands; persist sequence state. Couple reservations to stage cleanup.
2. Prefer existing engine mechanics and data settings before custom state. Keep all new outcome-affecting data in NativeState; no mutable module members.
3. Read `Persistence.h` for reservation/sequence/pity/perception adapters. Use separate keys per subsystem and validate failed loads explicitly; never silently reset a corrupt save.
4. For scalar defaults read `docs/gameplay-settings.md`; use generated readGameplaySettings and systems/Configured.h. Do not invent engine game.json keys.
5. Follow `examples/native/libraries/SystemsConsumer.cpp`, `tests/libraries/systems_tests.cpp` and `systems_engine_tests.cpp`.
6. Run catalog/generator checks, library tests, engine integration tests and external native/export tests. Verify rollback for game-specific integration; do not mutate ECS while iterating.

Pure policies link `AIVERSE::lib_action_sequences`; EngineMechanics/Persistence adapters require
`AIVERSE::lib_action_sequences` and the normal game host. Commands remain the only external
simulation input. Presentation previews cannot authorize mutations. Keep docs, versions,
examples and tests synchronized; describe exact coverage instead of claiming a full new engine.

Build selection: `AIVERSE::lib_action_sequences`; native OBJECT modules call
`aiverse_native_use_libraries(target action-sequences)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
