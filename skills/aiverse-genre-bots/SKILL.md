---
name: aiverse-genre-bots
description: Reuse AIVERSE genre bot policies (melee spacing/attack timing/dodge-parry/lock-on, shooter aim/lead/burst/reload/overheat/cover, squad roles with formations, objective choice) when native C++ action, shooter or team games need deterministic computer opponents.
---

# genre-bots

Use when a native/host game needs bot opponents for custom melee or shooter mechanics, squad/team roles or
objective selection. RTS economy/tactics stay with `rts-bots-rts` (RtsAI); the built-in FPS module bot stays with
`aiverse-bots-fps`. These policies are genre-independent and compose `combat-actions` and `weapon-policies`.

1. Read `docs/genre-packages.md` (genre-bots section) and the catalog entry; inspect
   `libraries/include/aiverselib/bots/{MeleeBot,ShooterBot,SquadRoles,ObjectivePolicy}.h` for exact behavior.
2. Responsibility: decisions only. `stepMeleeBot` returns `MeleeInput` (move wish, face yaw, attack request,
   dodge + direction, guard held, lock command); `stepShooterBot` returns `ShooterInput` (view yaw/pitch, move wish,
   `FeedInput` fire/reload, chosen cover); `stepSquad` returns role orders with formation goals; `chooseObjective`
   returns an objective id + stance. Feed them through the SAME authorization path as player input
   (`stepMelee`/`stepDodge`/`stepGuard`/`stepLockOn`, `stepFeed`/`stepRecoil`, navigation, commands). Never let a bot
   write health, positions, ammo or cooldowns. Difficulty never changes stats.
3. Perception is owner-supplied: fill `PerceivedTarget` (position, velocity, guarding, vulnerable, `meleeThreat`
   from the visible attack phase) only from visibility-authorized senses; mark unseen targets `visible=false` or
   omit them. Cover candidates (`protectionMilli`, `reachable`) and `ObjectiveInfo` (owner, progress, allies/enemies)
   come from the owner's LOS/navigation/match state.
4. Determinism: Fixed math, integer ticks, one call per bot per tick (repeated/backwards ticks return nullopt
   without touching state). RNG only through the passed `Random&` (use `sim.rng()` or an RNG saved with the sim);
   draws happen only for aim error, misreads, dodge-vs-guard and openings with chances strictly inside (0,1000).
5. Data: optional `genreBots` section of `libraries/spec/gameplay.json` (lead uses `aimSolver`). Load with
   `readGameplaySettings`, then `bots/GenreBotConfig.h`: `meleeBotConfig`, `shooterBotConfig`, `squadConfig`,
   `objectiveWeights`, `botDifficulty`; check `genreBotSettingsErrors`. Presets: `difficultyPreset(0|1|2)`.
   Example: `examples/libraries/genre/bot-settings.json`. Edit the spec and run
   `python3 tools/libraries/generate_gameplay.py`; never hand-edit generated files.
6. Persist `MeleeBotState`, `ShooterBotState`, `SquadState` with `genre::save/load` (record kinds 56..58, 59..63
   reserved) via `storeBytes/loadBytes` in their own NativeState keys; fold `genreBotHash` into the simulation
   checksum. Bind configs/attack sets/weapon definitions in the save envelope.
7. Example: `examples/native/libraries/GenreConsumer.cpp` (`genreBotConsumerExample`). Tests:
   `tests/libraries/genre_tests.cpp` (`genreBots`: melee and hitscan bot-vs-bot duels with rerun hashes,
   mid-fight save/restore and difficulty outcomes). Run `python3 tools/libraries/catalog.py check`,
   `generate_gameplay.py --check`, `tests/libraries/run.py`, `native_consumer.py`.
   Not implied: navigation/pathing (feed role goals to navmesh or RTS move commands), match/objective rules,
   senses/LOS, sports/racing/card/turn bots, learning or Elo-rated strength.

Build selection: `AIVERSE::lib_genre_bots` (depends on `AIVERSE::lib_combat_actions`, `AIVERSE::lib_weapon_policies`,
`AIVERSE::lib_configuration`, `aiverse_engine`, `aiverse_world`); native OBJECT modules call
`aiverse_native_use_libraries(target genre-bots)`. Only this header library and its declared dependencies are selected.
