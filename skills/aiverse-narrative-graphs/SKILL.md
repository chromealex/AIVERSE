---
name: aiverse-narrative-graphs
description: Reuse AIVERSE localized narrative graphs and social scenes (typed facts, relationships, knowledge, deferred consequences, cinematic cue requests, barks, visual-novel backlog/skip/auto) when building native C++ story, adventure, RPG or visual-novel games.
---

# narrative-graphs

1. Read `docs/genre-packages.md` (sections Configuration/Narrative and "Narrative scenes") and the catalog entry. Inspect
   `libraries/include/aiverselib/genre/NarrativeScenes.h` (0.2.0 runtime) or `Narrative.h` (legacy Story) for exact behavior.
2. Choose the API: simple choice graphs keep `readStory` / `chooseStory` / `StoryState` (unchanged). For relationships
   (affinity/trust/fear meters), who-knows-what, typed int/bool/enum facts, AND/OR/NOT conditions, one-shot flags, chapters/acts,
   deferred consequences, speakers/participants, cinematic cues, ambient barks or VN backlog/skip-read/auto-advance use
   `readNarrativeWorld` (`NarrativeSceneData.h`) + `beginNarrative` + `stepNarrative`. The reader also accepts the legacy story JSON.
3. Data is localization keys plus stable positive integer IDs; follow `examples/libraries/genre/story-social.json`. Unknown keys,
   unknown characters/facts/flags/chapters/nodes/consequences, broken edges and lines that cannot reach an ending are rejected with
   `narrative.<path>: reason`. Keep the world immutable for a running save.
4. Settings: optional `narrative` section of the gameplay settings (`examples/libraries/genre/narrative-settings.json`), loaded
   with readGameplaySettings, then `narrativeConfig` / `narrativeSettingsErrors` (`NarrativeConfigured.h`).
5. Call `stepNarrative(world, config, state, tick, input)` exactly once per authoritative tick with strictly increasing ticks.
   Player/bot actions arrive as Commands with strictly increasing IDs (StartScene, Advance, Choose, SetAuto, SetSkip); invalid or
   duplicate commands are rejected without side effects. Barks are requested per tick (`BarkRequest{id, trigger, speakers}`).
6. Consume `NarrativeReport`: events for UI/logic, `CueRequest` (`NarrativeCues.h`) for the cinematic owner in `sequence` order,
   `BarkLine` for subtitles/audio. The narrative policy never plays cues, audio, cameras or animation and never grants rewards; issue
   rewards through the existing transactional owner from committed events.
7. Host executables link AIVERSE::genre; registered native OBJECT modules call
   `aiverse_native_use_libraries(target narrative-graphs)`. Follow `narrativeSceneConsumerExample` in
   `examples/native/libraries/GenreConsumer.cpp`.
8. Persist `NarrativeState` with genre::save/load (record kind 64; kinds 64..71 reserved) and storeBytes/loadBytes in its own
   NativeState key; after load check `narrativeStateError(world, config, state)`; bind `narrativeWorldHash` and
   `narrativeConfigHash` in the save envelope; hash with `narrativeHash`. Never keep mutable state on a NativeModule instance.
9. Run catalog/generator checks, tests/libraries/run.py, native_consumer.py and aiverse_library_genre_tests. Report story policies
   separately from wired UI/cinematics and list unverified platforms.

Build selection: `AIVERSE::lib_narrative_graphs`; native OBJECT modules call
`aiverse_native_use_libraries(target narrative-graphs)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
