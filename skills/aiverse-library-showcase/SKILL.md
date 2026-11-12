---
name: aiverse-library-showcase
description: Reuse AIVERSE end-to-end real rts simulation, rmlui, save and meta reward example when working on native game projects and reusable engine tooling.
---

# library-showcase

Read `docs/native-toolkit.md`, `examples/libraries/showcase/main.cpp` and the matching catalog entry.

1. Create isolated content with showcase_project.py. Build the opt-in aiverse_library_showcase target and run --auto with a fresh output directory. Check actual build/attack events, save checksum round trip, idempotent meta rewards and screenshot. Reward storage is a single-process example, not a production backend. Use distinct match IDs for distinct authoritative matches.
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
