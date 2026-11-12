---
name: aiverse-typed-state
description: Reuse AIVERSE generate typed nativestate access and transactional migrations when working on native game projects and reusable engine tooling.
---

# typed-state

Read `docs/native-toolkit.md`, `tools/libraries/native_codegen.py` and the matching catalog entry.

1. Declare bounded int64 and bool fields. Use initialize only for empty setup state; load validates schema/ranges, store validates atomically. Define explicit rename/set/erase migration steps. Migration does not bypass engine nativeModules VERSION compatibility. Keep engine simulation state in NativeState.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.

Runtime library: none. This recipe integrates an existing engine subsystem or runs host tools;
it does not provide a separate runtime implementation. Pin the engine revision for that owner.
