---
name: aiverse-balance-lab
description: Reuse AIVERSE C++ mirrored round-robin cases and aggregate faction outcomes when implementing balance-lab in an external native game project or host tool.
---

# balance-lab

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Balance.h` and the matching catalog entry.

1. Use roundRobin with identical seed lists and swapped starts. Record actual player-to-faction mapping. Use addSample only for completed matches/draws; keep time limits separate. Run playlab.py against prepared project variations; report sample counts and seeds, not unsupported confidence claims.
2. Link `AIVERSE::lib_balance_lab` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::lib_balance_lab` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin balance-lab --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.

Build selection: `AIVERSE::lib_balance_lab`; native OBJECT modules call
`aiverse_native_use_libraries(target balance-lab)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
