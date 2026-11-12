---
name: aiverse-bots-fps
description: Reuse AIVERSE command-driven fps bots with perception, reaction, aiming, reload, retreat, zone and patrol in native C++ projects when implementing game bots or navigation.
---

# bots-fps

1. Read `docs/bots.md` and this catalog entry. Read `libraries/include/aiverselib/bots/Fps.h` for exact APIs.
2. Run on one authoritative host with map bot:false for owned slots. Use fpsCommand then sim.submit; never mutate actors. Persist FpsState with the simulation. Supply generated navmesh or authored waypoint graph; no bomb, loot or vehicle mechanics are implied.
3. Link `AIVERSE::fps_bots` from an executable host target. For registered OBJECT native modules use `aiverse_native_use_bots(aiverse_native_<id> RTS FPS)` with only the needed genres to avoid circular linkage. Use `examples/native/libraries/BotConsumer.cpp` for include/persistence access and `examples/libraries/bots/main.cpp` for actual host scheduling.
4. Keep configurations immutable and validate JSON at load. Store dynamic state explicitly; save and restore it with simulation before producing further commands. Never retain future state after rollback.
5. Keep sim Fixed/integer-only. Stable ties and deterministic operation budgets must not depend on wall time. Feed commands through the existing authority/network route.
6. Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, root bot/navmesh tests and representative real matches. If changing engine simulation run full checks and all templates.
7. Update package version, tests, examples and documentation together. Report actual coverage and unverified platforms; do not describe the current AABB navigation mesh as a triangle-navmesh importer or a streamed large-world solution.
