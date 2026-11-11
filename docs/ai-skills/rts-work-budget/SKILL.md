---
name: rts-work-budget
description: Reuse AIVERSE deterministic round-robin work with operation budgets when working on native game projects and reusable engine tooling.
---

# work-budget

Read `docs/native-toolkit.md`, `libraries/include/aiverselib/runtime/WorkBudget.h` and the matching catalog entry.

1. Keep WorkItems sorted by unique ID. Each callback performs one bounded operation without modifying the items span or cursor. Persist completed counters and WorkCursor for rollback. Failed callback must leave its own output untouched. Never budget by elapsed wall time or use unordered iteration.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.
