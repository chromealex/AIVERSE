---
name: aiverse-stealth-senses
description: Reuse AIVERSE stealth senses (sight cones, hearing with occlusion and noise budgets, footprint/scent trails, suspicion meters, ally alert propagation, search behaviour, disguises, hiding spots) when building native C++ stealth, horror, immersive-sim or action games and their bots.
---

# stealth-senses

Use when a native/host owner needs per-agent perception for guards, monsters or bots: who sees/hears/smells
whom, how suspicious they are, who gets alerted, where they search. Not for RTS fog of war: `modules/rts`
vision stays authoritative there. Bounded long-term memory/threat ranking can additionally use `ai-perception`
(`systems/Perception.h`): feed it from `PerceivedTarget` records, never from omniscient queries.

1. Read `docs/genre-packages.md` (stealth-senses section) and the catalog entry. Inspect
   `libraries/include/aiverselib/genre/Senses.h` for exact behaviour.
2. Responsibility: perception records and events only. No movement, pathing, damage or animation. The owner
   supplies observer poses (`SenseAgent`), target data (`SenseTarget`: light/cover/stance/visibility multipliers,
   zone, actions, disguise equip, hiding enter/exit, trail deposit), noise events and search candidates
   (`SearchCandidate`, e.g. navmesh points or hiding spots), plus two callbacks: `los(agent,target)` -> bool and
   `occlusion(agent,noise)` -> milli 0..1000. Callbacks must be pure queries; they are called in observer-id then
   target/noise order, only for in-range candidates, under `sight.maxQueries` / `hearing.maxQueries` budgets.
3. Data: scalar knobs live in the generated optional `senses` section of `libraries/spec/gameplay.json`
   (milli-metres, milli-turns, milli strengths, ticks). Load with `readGameplaySettings`, check
   `sensesSettingsErrors`, convert with `sensesConfig` (`genre/SensesConfigured.h`). Disguises and hiding spots:
   `readSenseDefinitions(json)` from `genre/SensesData.h` (example `examples/libraries/genre/senses.json`);
   errors are path-qualified `JsonError`s. Edit the spec and run `python3 tools/libraries/generate_gameplay.py`.
4. Per authoritative tick call `stepSenses(config, defs, state, tick, input, los, occlusion)` exactly once
   (strictly increasing ticks; invalid input or repeated ticks return nullopt and leave state untouched).
   Consume `SensesReport::perceived` (`PerceivedTarget`: observer, target, level, suspicion, lastKnown,
   lastSense, sensedNow, visibleNow, strengthNow, searching) and `events` (awareness changes, alerts, search
   start/point/found/give-up, disguise blown, hiding entered/refused/exited/revealed, dropped noises).
   Bots move toward `searchGoal(state, observer)`; they must report their new positions next tick.
5. Rules worth knowing: target 0 is an anonymous disturbance capped at Searching. Searching starts when the
   strongest Searching/Alert target is not sensed this tick; give-up drops the meter to `suspiciousAt`. Alerts go
   to same-team allies within radius after `alert.delay` ticks with the sender's last known position. Disguises
   scale gains for fooled teams by zone; listed actions blow them. Hidden targets are invisible to observers
   that did not see the entry, until a search reaches the spot's candidate. Absent observers are frozen.
6. Persist `SensesState` with `genre::save/load` (`genre/SensesPersistence.h`, record kinds 48..55 reserved,
   48..52 used) and `storeBytes/loadBytes` in a dedicated NativeState key; feed `sensesHash(state)` into the
   simulation checksum and bind `sensesConfigHash(config)` plus the definition version in the save envelope.
   Call `removeSenseAgent` when an entity is destroyed.
7. Example: `examples/native/libraries/GenreConsumer.cpp` (`librarySensesConsumerExample`). Run
   `python3 tools/libraries/catalog.py check`, `generate_gameplay.py --check`, `tests/libraries/run.py`
   (genre tests `stealthSenses`), `native_consumer.py`. Not implied: vision cones rendering, sound propagation
   through geometry (occlusion is the owner's), navmesh search routes, NPC dialogue/barks or RTS fog.

Build selection: `AIVERSE::lib_stealth_senses` (depends on `aiverse_engine` only); native OBJECT modules call
`aiverse_native_use_libraries(target stealth-senses)`. Only this header library and its declared dependencies
are selected. See `skills/catalog.json` for dependency routing.
