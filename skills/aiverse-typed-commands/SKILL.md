---
name: aiverse-typed-commands
description: Reuse AIVERSE generate typed native command encoders and dispatch when working on native game projects and reusable engine tooling.
---

# typed-commands

Read `docs/native-toolkit.md`, `tools/libraries/native_codegen.py` and the matching catalog entry.

1. Reserve a project-unique commandType in 32768..65535 and unique action IDs. Use encode then Simulation.submit. decode checks type, action, nonneutral player, exact argument count and bounds. Handlers must separately validate player range, ownership, resources and target lifetimes. Commands do not grant authority.
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
