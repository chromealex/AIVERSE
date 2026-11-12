---
name: aiverse-content-mods
description: Reuse AIVERSE C++ versioned dependency resolution, conflicts and deterministic mod order when implementing content-mods in an external native game project or host tool.
---

# content-mods

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Content.h` and the matching catalog entry.

1. Build ContentPackage records from trusted manifests, then resolveContent. Digest is required metadata, not computed or verified here; verify actual bytes through catalog pins/content tooling before mounting. Mount in returned order with explicit override rules. Never execute untrusted scripts or use manifest paths without validation.
2. Link `AIVERSE::lib_content_mods` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::lib_content_mods` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin content-mods --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.

Build selection: `AIVERSE::lib_content_mods`; native OBJECT modules call
`aiverse_native_use_libraries(target content-mods)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
