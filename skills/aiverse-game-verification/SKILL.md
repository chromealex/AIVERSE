---
name: aiverse-game-verification
description: Reuse AIVERSE game-verification for headless seed checks, snapshot/serialized restore gates and machine-readable reports in C++ native game projects.
---

# game-verification

Read `docs/gameplay-packages.md`, `libraries/catalog.json` and `tools/libraries/gameplay.py` from the engine root.
Read `docs/gameplay-settings.md` for supported settings; do not invent JSON keys or APIs.

## Workflow

1. Identify whether the existing engine mechanic already owns this behavior. Configure it first.
2. Run verify against the built project-specific headless executable. Set requireWinner for combat scenarios; tutorial/sandbox scenarios may intentionally have no winner. A successful headless run does not validate UI rendering.
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

Runtime library: none. This recipe integrates an existing engine subsystem or runs host tools;
it does not provide a separate runtime implementation. Pin the engine revision for that owner.
