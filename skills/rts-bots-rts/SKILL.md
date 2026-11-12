---
name: rts-bots-rts
description: Reuse AIVERSE rts bot profiles and transactional checkpoints over the existing rtsai in native C++ projects when implementing game bots or navigation.
---

# bots-rts

1. Read `docs/bots.md` and this catalog entry. Read `libraries/include/aiverselib/bots/Rts.h` for exact APIs.
2. Use existing RtsAI for economy, build/research, combat, garrisons and terrain/planet play. Host calls update before sim.step. rules.ai may override profiles. Preserve the legacy knowledge policy; do not claim fog-fairness. Save bot state with simulation; exact player/seed/settings must match.
3. Link `AIVERSE::aiverse_bots` from an executable host target. For registered OBJECT native modules use `aiverse_native_use_bots(aiverse_native_<id> RTS FPS)` with only the needed genres to avoid circular linkage. Use `examples/native/libraries/BotConsumer.cpp` for include/persistence access and `examples/libraries/bots/main.cpp` for actual host scheduling.
4. Keep configurations immutable and validate JSON at load. Store dynamic state explicitly; save and restore it with simulation before producing further commands. Never retain future state after rollback.
5. Keep sim Fixed/integer-only. Stable ties and deterministic operation budgets must not depend on wall time. Feed commands through the existing authority/network route.
6. Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, root bot/navmesh tests and representative real matches. If changing engine simulation run full checks and all templates.
7. Update package version, tests, examples and documentation together. Report actual coverage and unverified platforms; do not describe the current AABB navigation mesh as a triangle-navmesh importer or a streamed large-world solution.
