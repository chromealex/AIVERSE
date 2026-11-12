---
name: aiverse-hot-reload
description: Reuse AIVERSE stage and atomically activate validated host settings when working on native game projects and reusable engine tooling.
---

# hot-reload

Read `docs/native-toolkit.md`, `libraries/include/aiverselib/runtime/HotReload.h` and the matching catalog entry.

1. Stage with parser returning optional settings and a validator. Commit only at a safe point using expected revision. Failed staging clears pending data and preserves live settings. Handle parser exceptions in the host. Never hot-reload deterministic gameplay state using a filesystem clock; replicate gameplay changes through Commands.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.
