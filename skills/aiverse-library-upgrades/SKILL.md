---
name: aiverse-library-upgrades
description: Reuse AIVERSE read-only package upgrade plan from lock hashes when working on native game projects and reusable engine tooling.
---

# library-upgrades

Read `docs/native-toolkit.md`, `tools/libraries/project_checks.py` and the matching catalog entry.

1. Compare schema-2 libraries.lock.json with the current catalog/engine root. Review version changes, changed transitive files and required checks. The tool never updates pins or migrates code automatically. Run project tests and update pins only as an explicit reviewed project change.
2. Use the existing engine/library implementation rather than copying algorithms into a game.
3. Follow `examples/libraries/toolkit/schema.json` and `tests/libraries/toolkit_tools.py` for generators/tools; `tests/libraries/runtime_tests.cpp` for runtime APIs.
4. Run `python3 tests/libraries/toolkit_tools.py`, `python3 tests/libraries/run.py` and `python3 tools/libraries/catalog.py check`. For native integration also run `tests/libraries/native_consumer.py`.
5. Preserve deterministic state, ordered iteration and normal Command routing. Re-run restoration tests when integrating gameplay state.

Headers link through `AIVERSE::libraries`; engine adapters/generated modules use
`AIVERSE::gameplay_native`. Keep host clocks, UI and hot reload outside simulation.
Report exact tested coverage and platform limitations. Do not claim generated scaffolds
contain the project's gameplay behavior or that a generic reference selector is a complete schema.
