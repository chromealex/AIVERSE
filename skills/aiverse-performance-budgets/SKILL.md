---
name: aiverse-performance-budgets
description: Reuse AIVERSE C++ nearest-rank p95, peak limits and baseline regression checks when implementing performance-budgets in an external native game project or host tool.
---

# performance-budgets

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Performance.h` and the matching catalog entry.

1. Collect timing outside sim after warm-up on the same hardware/build/quality. Samples and budgets must share units. Use summarize and checkBudget; insufficient samples fail. Never let timing metrics affect deterministic gameplay. Report peak and sample count as well as p95.
2. Link `AIVERSE::lib_performance_budgets` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::lib_performance_budgets` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin performance-budgets --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.

Build selection: `AIVERSE::lib_performance_budgets`; native OBJECT modules call
`aiverse_native_use_libraries(target performance-budgets)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
