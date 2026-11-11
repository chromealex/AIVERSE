---
name: rts-change-planner
description: Reuse AIVERSE C++ dependency-ordered change recipes and verification checklist when implementing change-planner in an external native game project or host tool.
---

# change-planner

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Planner.h` and the matching catalog entry.

1. Describe explicit ChangeRecipe dependencies/files/checks and call planChanges. Unknown IDs and cycles fail. Review actual file diffs; the planner does not infer semantics or edit files. Apply supported rule mutations with existing gameplay.py recipe/apply and its stale-base checks.
2. Link `AIVERSE::libraries` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::gameplay_native` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin change-planner --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.
