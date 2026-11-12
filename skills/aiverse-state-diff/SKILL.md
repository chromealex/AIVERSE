---
name: aiverse-state-diff
description: Reuse AIVERSE locate the first differing native field and trace tick when working on native game projects and reusable engine tooling.
---

# state-diff

Read `docs/native-toolkit.md`, `libraries/include/aiverselib/runtime/StateDiff.h` and the matching catalog entry.

1. Use firstStateDifference for NativeState-compatible maps and arrays. Export canonical tick/modules snapshots from both runs, then use project_checks.py desync. Missing or differently sampled ticks are coverage errors. Only exported fields can be diagnosed; this does not automatically instrument every ECS pool.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::lib_state_diff`; engine adapters/generated modules use
`AIVERSE::lib_state_diff`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.

Build selection: `AIVERSE::lib_state_diff`; native OBJECT modules call
`aiverse_native_use_libraries(target state-diff)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
