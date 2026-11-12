---
name: aiverse-camera
description: Reuse AIVERSE camera for rts/follow/orbit target controls, limits, smoothing and strategic icon threshold in C++ native game projects.
---

# camera

Read `docs/gameplay-packages.md`, `libraries/catalog.json` and `libraries/include/aiverselib/gameplay/Camera.h` from the engine root.
Read `docs/gameplay-settings.md` for supported settings; do not invent JSON keys or APIs.

## Workflow

1. Identify whether the existing engine mechanic already owns this behavior. Configure it first.
2. Use delta time only in presentation. Apply resulting Pose to the existing client/editor camera. World-to-sphere mapping remains the planet camera responsibility.
3. Link AIVERSE::libraries for pure headers. Link AIVERSE::gameplay_native for NativeCommands.h; after aiverse_native_module, use target_link_libraries(aiverse_native_ID PRIVATE AIVERSE::gameplay_native).
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

Build selection: `AIVERSE::lib_camera`; native OBJECT modules call
`aiverse_native_use_libraries(target camera)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
