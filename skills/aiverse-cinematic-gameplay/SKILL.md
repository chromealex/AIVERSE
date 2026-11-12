---
name: aiverse-cinematic-gameplay
description: Reuse AIVERSE cinematic gameplay policies (time-scale domains with hit-stop/slow-motion, scene timelines of dialogue/animation/camera/sound/event cues, sync waits on host-reported progress with timeouts, QTEs with difficulty and accessibility, interaction staging with marks and input locks, checkpoints around scenes) when building native interactive-movie, adventure or story-action games.
---

# cinematic-gameplay

Use when a native/host owner needs authoritative scene flow: slow motion or hit-stop that stays exact, scripted
scenes whose cues fire once in a fixed order, waits for a dialogue line or animation marker that can never stall
the simulation, quick-time events, actors walking to marks, input lock during scenes, saves around scenes.
Not a cutscene renderer, audio/animation player or camera system: presentation plays `SceneEvent`s and reports
progress back. Animation clips/markers come from the existing animation system (`docs/animation-graphs.md`);
this package only references their IDs. Music-synced judgement belongs to `rhythm-timing`.

1. Read `docs/genre-packages.md` (cinematic-gameplay section: determinism model) and the catalog entry. Inspect
   `libraries/include/aiverselib/genre/Cinematic*.h` for exact behaviour.
2. Authority split. Authoritative (save, hash, identical on every peer): `TimeState` (requests, accumulators),
   `SceneState` (timeline position, emitted cues, wait/QTE outcomes, staging, input locks, queue). Host only:
   audio/animation/camera playback progress, navigation, teleport execution. The host reports
   `LineReport{cue,tick}`, `MarkerReport{cue,marker,tick}`, `ParticipantPose`/`arrived` and `QteInput{tick,button,down}`
   with authoritative ticks; reports outside (emission tick, current tick] are ignored (`ReportIgnored`).
3. Data: scene library JSON via `readSceneLibrary(json)` (`genre/CinematicData.h`, example
   `examples/libraries/genre/scene.json`): optional `domains`, `timePresets`, `scenes` with `marks`, `participants`,
   `cues`, `syncs` (`wait`: lineFinished / marker / inPosition, `timeout` required >= 1), `qtes`. Errors are
   path-qualified `JsonError`s (`cinematic.scenes[0].cues[2].kind: ...`, `cinematic.scene 10.sync 1.wait[0].cue: ...`).
   Scalars live in the optional generated `cinematic` settings section (`qteOptions`, `hitStopRequest`,
   `slowMotionRequest`, `cinematicSettingsErrors` in `genre/CinematicConfigured.h`).
4. Per authoritative tick, in this order: `stepTime(lib.time, time, tick, timeInput)`; then
   `stepScene(lib, scene, tick, input)` with `input.domainSteps = sceneDomainSteps(timeReport)`; then copy
   `report.timeAdd` / `report.timeCancelTags` into the next tick's `TimeInput` (one tick latency, deterministic).
   Strictly increasing ticks, call every tick; invalid input returns nullopt and leaves state untouched.
   Gameplay consumes essential `Cue` events (`Event`, `Move`, input lock/release; `flags & 1` = fired while
   skipping), `QteResolved`, `Finished`/`Aborted`, `TeleportRequested`, `SnapshotRequested`. Reject player
   commands for actors where `inputLocked(scene, participant)`.
5. Narrative bridge: a narrative `CueRequest{cueId,...}` becomes `cueTrigger(seq, cueId, bindings)` in
   `SceneInput::commands`; `sceneForCue(lib, cueId)` tells whether a scene listens to it. Bindings map scene
   participants to entities. Triggers while a scene runs are queued (at most 8).
6. Skip/seek/resume: `Skip` jumps to the next sync point, `SkipScene` to the end (essential cues still fire,
   presentation cues are recorded as skipped, QTEs in between resolve `Skipped`, movers teleport). After loading
   a save send `ResumeCheckpoint`: the timeline rewinds to the last sync point reached and replays presentation
   cues; essential cues never fire twice. `SnapshotRequested` value 0/1/2 = before scene / checkpoint sync / after.
7. Persist `TimeState` and `SceneState` with `genre::save/load` (`genre/CinematicPersistence.h`, record kinds
   72..79 reserved, 72..75 used) in dedicated NativeState keys; feed `cinematicHash` into the checksum and bind
   `sceneLibraryHash(lib)` (scene versions included) in the save envelope; a state saved against another scene
   version is rejected.
8. Example: `examples/native/libraries/GenreConsumer.cpp` (`libraryCinematicConsumerExample`). Run
   `python3 tools/libraries/catalog.py check`, `generate_gameplay.py --check`, genre tests (`cinematicGameplay`)
   and `native_consumer.py`. Not implied: cutscene editor, camera rigs, lip sync, audio timing, root motion.

Build selection: `AIVERSE::lib_cinematic_gameplay` (depends on `aiverse_engine` only); native OBJECT modules call
`aiverse_native_use_libraries(target cinematic-gameplay)`. Only this header library and its declared dependencies
are selected. See `skills/catalog.json` for dependency routing.
