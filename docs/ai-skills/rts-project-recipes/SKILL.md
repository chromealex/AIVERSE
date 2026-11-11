---
name: rts-project-recipes
description: Reuse AIVERSE project-recipes for plan and validated application of resource/building/ability/faction changes in C++ native game projects.
---

# project-recipes

Read `docs/gameplay-packages.md`, `libraries/catalog.json` and `tools/libraries/gameplay.py` from the engine root.
Read `docs/gameplay-settings.md` for supported settings; do not invent JSON keys or APIs.

## Workflow

1. Identify whether the existing engine mechanic already owns this behavior. Configure it first.
2. Generate a plan, inspect checks, then apply with the real project-specific headless executable. Apply validates a staged copy before replacing rules.json and retains a content-addressed backup. Never overwrite a stale plan.
3. Invoke python3 tools/libraries/gameplay.py --help; tools run on the host, not in the game.
4. Load project-native settings from config using readGameplaySettings; copy validated persistent parameters into NativeState at setup. Presentation state stays separate.
5. Use `examples/native/libraries` and `tests/libraries/gameplay_tests.cpp` as compiling examples.
6. Verify with the commands below and report the exact implemented coverage and unverified platforms.

## Invariants

- Simulation uses integer/Fixed values and engine RNG; no wall clock or view feedback.
- The library does not automatically serialize custom objects. Persist their fields through NativeState and reconstruct transient structures after rollback.
- Commands must check ownership/targets/resources at execution time. UI previews are advisory.
- Never call low-level mutation from presentation, bypass Command replication, or fork an existing engine mechanic.
- This package exposes host integration points; it does not silently install a new editor window or replace game UI.

## Validation

Run `python3 tools/libraries/generate_gameplay.py --check`, `python3 tools/libraries/catalog.py check`,
`python3 tests/libraries/run.py`, `python3 tests/libraries/gameplay_tools.py` and
`python3 tests/libraries/native_consumer.py`. For sim integration run project-specific headless
restore checks and all sample matches as required by AGENTS.md. Pin the package and its transitive
headers with catalog.py; review pin changes when upgrading.
