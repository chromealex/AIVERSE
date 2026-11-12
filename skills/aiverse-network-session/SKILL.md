---
name: aiverse-network-session
description: Reuse AIVERSE C++ transport-independent lobby, reconnect and ordered delivery policy when implementing network-session in an external native game project or host tool.
---

# network-session

Read `docs/application-packages.md`, `libraries/include/aiverselib/app/Session.h` and the matching catalog entry.

1. Provide transport/authentication outside this package. Use epoch on each async callback; retain sequence across reconnect and reset on a new session. Content hashes must come from verified manifests. accept is for ordered message streams, not out-of-order UDP or multiple peer channels. Do not present this as a server or netcode implementation.
2. Link `AIVERSE::lib_network_session` from the project's native target; keep host presentation and tools outside NativeModule tick callbacks. Use `AIVERSE::lib_network_session` only for engine Command adapters.
3. Follow `examples/libraries/app` and `tests/libraries/app_tests.cpp` for compiling API usage. For rewards also read `tests/libraries/app_meta_tests.cpp` and `skills/meta-game/SKILL.md`.
4. Pin with `python3 tools/libraries/catalog.py pin network-session --project /absolute/project`. Check the pin on upgrades; never copy a fork into the game.
5. Run `python3 tests/libraries/run.py`, `python3 tests/libraries/native_consumer.py`, `python3 tools/libraries/catalog.py check`. Run `python3 tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools` for tool workflows.

Keep coverage claims exact. This package does not install client/editor hooks automatically.
Persist any gameplay-affecting state through NativeState and include it in restore checks.
For visual wiring inspect real engine screenshots; compile-only RmlUi checks are not visual QA.

Build selection: `AIVERSE::lib_network_session`; native OBJECT modules call
`aiverse_native_use_libraries(target network-session)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
