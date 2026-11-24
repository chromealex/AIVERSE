---
name: aiverse-work-budget
description: Reuse AIVERSE deterministic round-robin work with operation budgets when working on native game projects and reusable engine tooling.
---

# work-budget

Read `docs/native-toolkit.md`, `libraries/include/aiverselib/runtime/WorkBudget.h` and the matching catalog entry.

1. Keep WorkItems sorted by unique ID. Each callback performs one bounded operation without modifying the items span or cursor. Persist completed counters and WorkCursor for rollback. Failed callback must leave its own output untouched. Never budget by elapsed wall time or use unordered iteration.
2. For ongoing ordered workloads, use `planRecurringWork(count, desiredPeriodTicks, maxVisitsPerTick)` and `nextRecurringIndex(count, cursor)`. Zero cap means no explicit cap; zero desired period is invalid. The returned visitsPerTick and maxRevisitTicks are exact visit-count bounds for an unchanged ordered set, not wall-clock performance guarantees. Persist the uint32 cursor in the owning simulation resource; overflow preserves the next index. Do not use this policy to skip physics, collision, or mandatory events. The MCP `simulation_budget_plan` previews the same policy without editing the world.
3. Use the existing engine/library implementation rather than copying algorithms into a game.
4. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
5. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
6. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::lib_work_budget`; engine adapters/generated modules use
`AIVERSE::lib_work_budget`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.

Build selection: `AIVERSE::lib_work_budget`; native OBJECT modules call
`aiverse_native_use_libraries(target work-budget)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
