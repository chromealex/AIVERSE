---
name: aiverse-sports-rules
description: Reuse AIVERSE ball-sport policies (possession, tackles, passes and shots with seeded accuracy, interception, goals, out of bounds and restarts, periods with stoppage and golden goal, fouls and penalties, role-based formations, positioning bot) when building native football, hockey, handball-like or arcade sports games.
---

# sports-rules

Use when a native/host owner needs the rules of a team ball game and AI teammates/opponents: who has the ball,
tackles, passes, shots, goals, throw-ins/corners/goal kicks/free kicks/penalties/face-offs, the match clock and
formations. Not a body controller: player movement, animation and collision belong to the owner (or
`character-locomotion`); the scoreboard UI is presentation; ball height/spin and offside are not modelled.

1. Read `docs/genre-packages.md` (sports-rules) and the catalog entry. Inspect `genre/SportsRules.h`,
   `genre/SportsFormation.h` and `bots/SportsBot.h` for exact behaviour.
2. Field frame `Vec2{x along the length, y across}`, goals at x = +-halfLength; team index 0 attacks +x in odd
   periods (`swapEnds`). `sportsAttackDirection(rules, state, team)` gives the current direction.
3. Data: `readSportsDefinitions(json)` (`genre/SportsData.h`, example
   `examples/libraries/genre/vehicles-sports/sports.json`): `field` (boundary lines|walls|faceoff, penalty area,
   face-off spots) and `formations` (slots with role keeper|defender|midfielder|forward, base/attack/defend
   fractions, ball pull). Scalar rules: optional generated `sports` settings section (`sportsRulesConfig`,
   `bots::sportsBotConfig`, `sportsSettingsErrors`); `clockMode` 0 running, 1 stopped, 2 added stoppage.
4. Per authoritative tick: bots (`bots::stepSportsBot` per team -> move targets + `SportsAction`s) or player
   commands -> the owner moves bodies -> `stepSports(rules, field, state, tick, dt, players, actions, rng)` with
   every player snapshot. Order inside: restart taking, actions by player id, ball motion, pickups/interceptions
   along the ball path, goals/boundaries, clock. RNG only through the passed `Random` (`sim.rng()` or a saved
   RNG); invalid input returns nullopt with state and RNG untouched.
5. Contests: tackles need range, an unlocked holder and no stun; success chance from tackle vs control skills;
   tackles from behind / failed tackles may be fouls -> free kick, or penalty inside the fouler's penalty area.
   Passes lead a moving receiver; accuracy skill scales the seeded angle error. Fast balls are controlled with
   `interceptMilli`; pickups pick the earliest closest approach, then distance, then id.
6. Persist `SportsState` 92 and `bots::SportsBotState` 95 (`genre/SportsPersistence.h`, 93 reserved, 88..95 shared
   with vehicle-controllers) plus the RNG state; fold `sportsHash` into the checksum and bind
   `sportsDefinitionsHash(field, rules, formations)` in the save envelope.
7. Examples: `examples/native/libraries/VehiclesSportsConsumer.cpp` and the executable 3v3 match in
   `examples/libraries/genre/vehicles-sports`. Run `catalog.py check`, `generate_gameplay.py --check`,
   `aiverse_library_vehicles_sports_tests` and the `library_vehicles_sports_example` CTest.

Build selection: `AIVERSE::lib_sports_rules` (depends on `configuration`, `aiverse_engine`); native OBJECT
modules call `aiverse_native_use_libraries(target sports-rules)`. See `skills/catalog.json`.
