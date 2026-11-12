---
name: aiverse-ai-perception
description: Reuse AIVERSE C++ bounded observation memory, expiration and threat selection for native game projects instead of generating duplicate gameplay algorithms.
---

# ai-perception

Read `docs/gameplay-systems.md`, `libraries/include/aiverselib/systems/Perception.h` and this package's catalog entry.

1. Feed only visibility-authorized observations; never enumerate hidden enemies into memory. Keep known positions after lost sight until tick TTL expires. Highest threat ties by smaller entity ID; capacity evicts oldest, then smaller ID, and rejects older incoming observations. Use stable packed IDs and clear destroyed targets only when knowledge permits. Persist via Persistence.h.
2. Prefer existing engine mechanics and data settings before custom state. Keep all new outcome-affecting data in NativeState; no mutable module members.
3. Read `Persistence.h` for reservation/sequence/pity/perception adapters. Use separate keys per subsystem and validate failed loads explicitly; never silently reset a corrupt save.
4. For scalar defaults read `docs/gameplay-settings.md`; use generated readGameplaySettings and systems/Configured.h. Do not invent engine game.json keys.
5. Follow `examples/native/libraries/SystemsConsumer.cpp`, `tests/libraries/systems_tests.cpp` and `systems_engine_tests.cpp`.
6. Run catalog/generator checks, library tests, engine integration tests and external native/export tests. Verify rollback for game-specific integration; do not mutate ECS while iterating.

Pure policies link `AIVERSE::libraries`; EngineMechanics/Persistence adapters require
`AIVERSE::gameplay_native` and the normal game host. Commands remain the only external
simulation input. Presentation previews cannot authorize mutations. Keep docs, versions,
examples and tests synchronized; describe exact coverage instead of claiming a full new engine.
