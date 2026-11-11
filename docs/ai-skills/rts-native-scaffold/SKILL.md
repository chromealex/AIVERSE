---
name: rts-native-scaffold
description: Reuse AIVERSE generate a registered external native c++ module when working on native game projects and reusable engine tooling.
---

# native-scaffold

Read `docs/native-toolkit.md`, `tools/libraries/native_codegen.py` and the matching catalog entry.

1. Generate into a new directory from a reviewed schema. Never overwrite custom module handlers; review the generated diff. Merge rules.fragment.json into nativeModules explicitly. Run the generated Smoke.cpp and external-native tests. Module.cpp intentionally has no game-specific effects.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.
