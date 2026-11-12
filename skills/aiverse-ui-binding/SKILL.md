---
name: aiverse-ui-binding
description: Reuse AIVERSE C++ snapshot ui bindings, escaped values and bounded action queues when implementing ui-binding in an external native game project or host tool.
---

# ui-binding

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Binding.h` and the matching catalog entry.

1. Use BindingModel.set/drain on a read-only snapshot. Invalidate after document reload or language change. Dispatch Actions through host Command routing. Do not retain simulation references.
2. Link `AIVERSE::libraries` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::gameplay_native` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin ui-binding --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.
