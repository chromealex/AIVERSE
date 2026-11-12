---
name: aiverse-bounded-memory
description: Reuse AIVERSE fixed-capacity scratch buffers and generational slot pools when working on native game projects and reusable engine tooling.
---

# bounded-memory

Read `docs/native-toolkit.md`, `libraries/include/aiverselib/runtime/Bounded.h` and the matching catalog entry.

1. Choose capacity from a documented budget; handle full buffers/pools explicitly. Stale or released handles fail. Slot indices are allocated in deterministic order. Capacity bounds objects, not heap memory inside T. Use fixed-size element types in hot paths. Persist pool/handle state through NativeState if it affects outcomes.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.
