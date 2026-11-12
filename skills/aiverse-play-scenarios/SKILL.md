---
name: aiverse-play-scenarios
description: Reuse AIVERSE C++ tick-indexed actions and assertions for automated play when implementing play-scenarios in an external native game project or host tool.
---

# play-scenarios

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Scenario.h` and the matching catalog entry.

1. Validate Scenario once before running. Advance at every exact tick; emit normal Commands via callback and supply authoritative metric snapshots. Missing metrics and skipped ticks fail. Reset the runner for each run; do not reuse it after rollback without restoring cursor state.
2. Link `AIVERSE::lib_play_scenarios` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::lib_play_scenarios` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin play-scenarios --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.

Build selection: `AIVERSE::lib_play_scenarios`; native OBJECT modules call
`aiverse_native_use_libraries(target play-scenarios)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
