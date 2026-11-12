---
name: aiverse-feedback
description: Reuse AIVERSE feedback for event-to-sound/particle/animation/notification/camera actions with cooldowns in C++ native game projects.
---

# feedback

Read `docs/gameplay-packages.md`, `libraries/catalog.json` and `libraries/include/aiverselib/gameplay/Feedback.h` from the engine root.
Read `docs/gameplay-settings.md` for supported settings; do not invent JSON keys or APIs.

## Workflow

1. Identify whether the existing engine mechanic already owns this behavior. Configure it first.
2. Bind dispatch callbacks to existing presentation services. Use only accepted sim events; clear FeedbackState after rollback/seek and never feed it into gameplay logic.
3. Link `AIVERSE::lib_feedback` (native OBJECT modules: `aiverse_native_use_libraries(aiverse_native_ID feedback)`); do not link the aggregate AIVERSE::libraries. Add AIVERSE::gameplay_native only when you include NativeCommands.h: `target_link_libraries(aiverse_native_ID PRIVATE AIVERSE::gameplay_native)`.
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

Build selection: `AIVERSE::lib_feedback`; native OBJECT modules call
`aiverse_native_use_libraries(target feedback)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
