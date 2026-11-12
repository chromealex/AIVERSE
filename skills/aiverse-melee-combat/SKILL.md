---
name: aiverse-melee-combat
description: Reuse AIVERSE custom melee timing, buffered combos, active-window hit deduplication and defense outcomes when building native C++ games across genres.
---

# melee-combat

1. Read `docs/genre-packages.md` and the catalog entry. Inspect `libraries/include/aiverselib/genre/Melee.h` for exact runtime behavior.
2. Prefer existing engine mechanics for built-in FPS movement/ammo, RTS abilities/damage and victory. Use these policies for an explicit custom owner; never maintain duplicate health, inventory or cooldown state.
3. Load scalar JSON through generated readGameplaySettings, then genre/Configured.h adapters. Read narrative definitions with readStory; use stable localization/fact/choice IDs. Keep definitions immutable for a running save.
4. Host executables link AIVERSE::genre. Registered native OBJECT modules call aiverse_native_use_genre(aiverse_native_<id>) to propagate collision dependencies without a circular game link. Follow examples/native/libraries/GenreConsumer.cpp.
5. Route external actions as Commands, validate ownership/resources/targets, and consume policy outcomes in the authoritative owner. Presentation only reads committed state. Use sim.rng for spread, integer ticks and Fixed arithmetic.
6. Persist state with genre::save/load and storeBytes/loadBytes in a separate NativeState key. Bind configuration/definition versions in the save envelope. Do not put mutable state on a NativeModule instance or keep future state after rollback.
7. Run catalog/generator checks, tests/libraries/run.py, native_consumer.py and aiverse_library_genre_tests. Add real project replay/collision/UI checks for integration changes. Report policies separately from fully wired genre gameplay and list platform gaps.
