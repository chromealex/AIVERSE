---
name: aiverse-boss-encounters
description: Reuse AIVERSE boss-encounters policies (telegraphed circle/cone/line/ring attacks with windup/active/recovery, slow/stun/burn statuses, boss phases/patterns/enrage/adds, encounter windows with personal bosses and archetype rotation, mob wander/hunt/flee/leash/swarm) when a native or host game needs bosses, telegraphed attacks or PvE mob behaviour.
---

# boss-encounters

Use for top-down action / extraction / survivor games (Drone Fall style) that need telegraphed boss attacks,
boss fights with phases, timed boss windows or crowds of hunting mobs. RTS games don't wire it by hand: rules
`encounters` (docs/encounters.md, `templates/boss_arena`) runs these policies on real units (bosses, mobs,
telegraphs as unit abilities, rules statuses, schedule, Lua events, decals); the steps below are for native/host
owners and for planning the same mechanics as data.

1. Read `docs/genre-packages.md` (boss-encounters section) and the catalog entry; inspect
   `libraries/include/aiverselib/genre/{Telegraphs,StatusEffects,BossEncounter,BossSchedule,MobBehaviour}.h`.
2. Data first: write one encounter JSON (`examples/libraries/genre/boss-encounter.json` is the template: statuses,
   attacks, bosses, schedule, mob). Integers only: `...Milli` lengths/angles/multipliers, times in ticks. Validate with
   `aiverse_encounter validate <file> --json`; errors name the path (`attack 4.statuses: unknown status 9`).
3. Tick order per owner tick: `stepStatuses` for each actor (burn damage, speed/stun) -> `stepBoss` (input HP, XZ
   distance to its combat target, `busy` while its telegraph runs, living adds per wave) -> on `Attack` call
   `beginTelegraph` toward the target -> `stepMob` for adds/mobs -> `stepTelegraph` every live instance with the
   hittable bodies -> apply each `TelegraphHit` through the existing damage owner (scale with `bossDamage`) and its
   status ids with `applyStatus` (or engine statuses) -> move bodies. Hold the caster still while
   `telegraphLocksMovement`; move a sweeping charge to `sweepReach`.
4. Authority: these functions decide and report; they never write health, positions, spawns or views. Feed
   `MobDecision::desired`/`goal` to steering, flow fields (RTS move orders) or navmesh. Perception (candidates) must be
   visibility-authorized. Personal bosses: `stepSchedule` with the participants, place the boss for each `Pending`
   request, then `markBossSpawned` / `markBossDefeated` (kills only) / `cancelBossRequests` (leaving is not a win);
   gate ordinary waves with `ordinaryWavesAllowed`. Persist `BossRotation` in the player profile between matches.
5. Views: draw `TelegraphView` (shape, pose, `progressMilli` fill) with `telegraphDecal` -> `RtsGroundCanvas` /
   Lua `view.draw_ground`; status visuals follow `StatusViewEvent` (one per instance, Ground or ModelTop anchor).
   Presentation reads these records only.
6. Determinism: Fixed math, integer ticks, one step per object per tick (repeated/backwards ticks return nullopt and
   leave the state unchanged); RNG only through the passed `Random&` (sim.rng() or one saved with the sim): weighted
   patterns, variant choice and wander legs. Targets are processed in ascending id.
7. Persist `TelegraphInstance`, `StatusActorState`, `BossState`, `ScheduleState`, `MobState`, `BossRotation` with
   `genre::save/load` (record kinds 112..117, 118..119 reserved) via `storeBytes/loadBytes` in their own NativeState
   keys; fold `encounterHash` into the checksum and bind `encounterDataHash` of the data file in the save envelope.
8. Check: `aiverse_encounter simulate <file> [--boss ID] [--seed N] [--reaction N] [--json]` runs the headless
   hero-vs-boss reference fight (exit 0 = boss defeated). Tests: `tests/libraries/encounter_tests.cpp`
   (`library_encounters`). Example consumer: `examples/native/libraries/EncounterConsumer.cpp`.
   Not implied: projectiles in flight (use weapon-policies), knockback/push physics, navigation, loot/rewards
   (loot-tables / meta), HUD banners, network authority.

Build selection: `AIVERSE::lib_boss_encounters` (depends on `AIVERSE::lib_combat_actions`, `aiverse_engine`); native
OBJECT modules call `aiverse_native_use_libraries(target boss-encounters)`. Only this header library and its declared
dependencies are selected.
