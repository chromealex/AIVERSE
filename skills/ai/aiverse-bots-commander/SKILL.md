---
name: aiverse-bots-commander
description: Reuse the AIVERSE side-commander bot (economy, build/research, waves, garrisons, terrain and planet play; profiles and transactional checkpoints over ComputerPlayer) for any project that has computer-controlled sides.
---

# Side commander bot (package `bots-commander`, legacy id)

Host: the side commander (`ComputerPlayer`) is a system of `engine/game` (GameCore, the one host for every project).

1. Read `docs/bots.md` and this catalog entry. Read `libraries/include/aiverselib/bots/Rts.h` for exact APIs.
2. Use existing ComputerPlayer for economy, build/research, combat, garrisons and terrain/planet play. Host calls update before sim.step. rules.ai may override profiles. Preserve the legacy knowledge policy; do not claim fog-fairness. Save bot state with simulation; exact player/seed/settings must match.
3. Link `AIVERSE::lib_bots_commander` from an executable host target. For registered OBJECT native modules use `aiverse_native_use_bots(aiverse_native_<id> COMMANDER COMBATANT)` with only the bot backends the module uses (legacy argument names `RTS`, `FPS` still accepted) to avoid circular linkage. Use `examples/native/libraries/BotConsumer.cpp` for include/persistence access and `examples/libraries/bots/main.cpp` for actual host scheduling.
4. Keep configurations immutable and validate JSON at load. Store dynamic state explicitly; save and restore it with simulation before producing further commands. Never retain future state after rollback.
5. Keep sim Fixed/integer-only. Stable ties and deterministic operation budgets must not depend on wall time. Feed commands through the existing authority/network route.
6. Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, root bot/navmesh tests and representative real matches. If changing engine simulation run full checks and all templates.
7. Update package version, tests, examples and documentation together. Report actual coverage and unverified platforms; do not describe the current AABB navigation mesh as a triangle-navmesh importer or a streamed large-world solution.

Build selection: `AIVERSE::lib_bots_commander`; native OBJECT modules call
`aiverse_native_use_libraries(target bots-commander)`. Only this header library and its
declared dependencies are selected. Requires the existing `aiverse_game` owner. See `skills/catalog.json` for dependency and responsibility routing.
