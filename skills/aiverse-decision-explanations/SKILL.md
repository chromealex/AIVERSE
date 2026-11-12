---
name: aiverse-decision-explanations
description: Reuse AIVERSE C++ bounded structured decision facts using existing reason codes for native game projects instead of generating duplicate gameplay algorithms.
---

# decision-explanations

Read `docs/gameplay-systems.md`, `libraries/include/aiverselib/systems/Decisions.h` and this package's catalog entry.

1. Collect facts from the actual authoritative calculation, including localization key/subject/required/actual/reason. explain keeps the first failure even when fact output is truncated. Do not infer damage/modifier explanations from guessed formulas. Send display-only reports to UI/debug consumers; keep diagnostics out of state hashes unless they drive behavior.
2. Prefer existing engine mechanics and data settings before custom state. Keep all new outcome-affecting data in NativeState; no mutable module members.
3. Read `Persistence.h` for reservation/sequence/pity/perception adapters. Use separate keys per subsystem and validate failed loads explicitly; never silently reset a corrupt save.
4. For scalar defaults read `docs/gameplay-settings.md`; use generated readGameplaySettings and systems/Configured.h. Do not invent engine game.json keys.
5. Follow `examples/native/libraries/SystemsConsumer.cpp`, `tests/libraries/systems_tests.cpp` and `systems_engine_tests.cpp`.
6. Run catalog/generator checks, library tests, engine integration tests and external native/export tests. Verify rollback for game-specific integration; do not mutate ECS while iterating.

Pure policies link `AIVERSE::lib_decision_explanations`; EngineMechanics/Persistence adapters require
`AIVERSE::lib_decision_explanations` and the normal game host. Commands remain the only external
simulation input. Presentation previews cannot authorize mutations. Keep docs, versions,
examples and tests synchronized; describe exact coverage instead of claiming a full new engine.

Build selection: `AIVERSE::lib_decision_explanations`; native OBJECT modules call
`aiverse_native_use_libraries(target decision-explanations)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
