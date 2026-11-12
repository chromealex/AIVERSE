---
name: aiverse-match-rules
description: Reuse AIVERSE match rules (team score events, rounds, overtime, draws) and objective modes (capture/hold zones, king of the hill, payload, elimination with lives/respawn waves, extraction, bomb/defuse, race/route gates, contested overtime, spectator follow order) when building native C++ shooter, MOBA, sports, racing or co-op games.
---

# match-rules

1. Read `docs/genre-packages.md` and the catalog entry. Inspect `libraries/include/aiverselib/genre/Match.h` for exact runtime behavior.
2. Prefer existing engine mechanics for built-in FPS movement/ammo, RTS abilities/damage and victory. Use these policies for an explicit custom owner; never maintain duplicate health, inventory or cooldown state.
3. Load scalar JSON through generated readGameplaySettings, then genre/Configured.h adapters. Read narrative definitions with readStory; use stable localization/fact/choice IDs. Keep definitions immutable for a running save.
4. Host executables link AIVERSE::genre. Registered native OBJECT modules call aiverse_native_use_genre(aiverse_native_<id>) to propagate collision dependencies without a circular game link. Follow examples/native/libraries/GenreConsumer.cpp.
5. Route external actions as Commands, validate ownership/resources/targets, and consume policy outcomes in the authoritative owner. Presentation only reads committed state. Use sim.rng for spread, integer ticks and Fixed arithmetic.
6. Persist state with genre::save/load and storeBytes/loadBytes in a separate NativeState key. Bind configuration/definition versions in the save envelope. Do not put mutable state on a NativeModule instance or keep future state after rollback.
7. Run catalog/generator checks, tests/libraries/run.py, native_consumer.py and aiverse_library_genre_tests. Add real project replay/collision/UI checks for integration changes. Report policies separately from fully wired genre gameplay and list platform gaps.

## Objective modes (match-rules 0.2.0)

Headers: `genre/MatchObjectives.h` (policies), `genre/MatchObjectivesConfigured.h` (settings adapters),
`genre/MatchObjectivesData.h` (map layout JSON). Match.h is unchanged; read docs/genre-packages.md § Match objectives.

1. Responsibility: per-tick objective state machines that return `ObjectiveEvent`s. The authoritative owner
   reports presence (`Occupants` team -> bodies, e.g. via `insideZone`), deaths, pickups, defusers and gate
   crossings (`crossesGate`) after its own spatial/team checks; never accept them from clients directly.
2. Steppers (each rejects repeated/backwards ticks and invalid input without mutation): `stepZone` (Freeze or
   Majority contest, decay, neutralizeFirst, hold scoring; `kingOfTheHill`, `activeHill`), `stepPayload`
   (`makePayloadTrack`/`payloadPoint`), `stepElimination` (lives, respawn waves, wipe), `stepExtraction`,
   `stepBomb` (single carrier, plant/defuse/kit timers, interruption; the fuse wins ties), `stepRoute`
   (`routeStandings`), `stepClock`/`extendClock` (checkpoint bonuses, contested overtime), `stepSpectator`
   (`spectatorDied`/`spectatorRespawned`; kill cam, then teammates by ascending ID, then enemies if allowed).
3. Scores: `awardObjectiveEvents(match, scoring, events, nextEvent)` commits all awards or none through
   `awardScore`; `awardObjectiveWin` makes a team the sole leader so the next `stepMatch` ends the round.
   Built-in RTS/FPS victory modes keep their own owners; do not run both for the same round.
4. Data: optional `objectives` section in `libraries/spec/gameplay.json` (regenerate with
   `generate_gameplay.py`), then `objectiveSettingsErrors`; layouts with `readObjectiveLayout`
   (`examples/libraries/genre/objectives.json`, settings `examples/libraries/genre/objective-settings.json`).
5. Persist each state with `genre::save/load` (record kinds 40..47) in its own NativeState key and fold
   `hashObjective` (states and per-tick events) into the checksum. Bind settings/layout versions in the envelope.
6. Example: `libraryMatchObjectivesConsumerExample` in `examples/native/libraries/GenreConsumer.cpp`;
   tests: `matchObjectives` in `tests/libraries/genre_tests.cpp`. Not implied: navigation/cart physics,
   weapon/bomb presentation, HUD, camera rigs, matchmaking or network spectating.

Build selection: `AIVERSE::lib_match_rules`; native OBJECT modules call
`aiverse_native_use_libraries(target match-rules)`. Only this header library and its
declared dependencies (`aiverse_engine` for Fixed math and JSON) are selected. See `skills/catalog.json` for dependency and responsibility routing.
