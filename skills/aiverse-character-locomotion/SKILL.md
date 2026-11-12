---
name: aiverse-character-locomotion
description: Reuse AIVERSE fixed character movement policy (ground/air acceleration, jump buffer, coyote time) and its mode machine for slopes, ledges, ladders, swimming, wall climbing and mantle/vault when building native C++ games across genres.
---

# character-locomotion

1. Read `docs/genre-packages.md` and the catalog entry. Inspect `libraries/include/aiverselib/genre/Locomotion.h` for exact runtime behavior.
2. Prefer existing engine mechanics for built-in FPS movement/ammo, RTS abilities/damage and victory. Use these policies for an explicit custom owner; never maintain duplicate health, inventory or cooldown state.
3. Load scalar JSON through generated readGameplaySettings, then genre/Configured.h adapters. Read narrative definitions with readStory; use stable localization/fact/choice IDs. Keep definitions immutable for a running save.
4. Host executables link AIVERSE::genre. Registered native OBJECT modules call aiverse_native_use_genre(aiverse_native_<id>) to propagate collision dependencies without a circular game link. Follow examples/native/libraries/GenreConsumer.cpp.
5. Route external actions as Commands, validate ownership/resources/targets, and consume policy outcomes in the authoritative owner. Presentation only reads committed state. Use sim.rng for spread, integer ticks and Fixed arithmetic.
6. Persist state with genre::save/load and storeBytes/loadBytes in a separate NativeState key. Bind configuration/definition versions in the save envelope. Do not put mutable state on a NativeModule instance or keep future state after rollback.
7. Run catalog/generator checks, tests/libraries/run.py, native_consumer.py and aiverse_library_genre_tests. Add real project replay/collision/UI checks for integration changes. Report policies separately from fully wired genre gameplay and list platform gaps.

Build selection: `AIVERSE::lib_character_locomotion`; native OBJECT modules call
`aiverse_native_use_libraries(target character-locomotion)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.

## Mode machine (slopes, ledges, ladders, swimming, climbing, mantle)

Include `<aiverselib/genre/LocomotionModes.h>` (plus `genre/Persistence.h` for save/load). Use it
instead of writing per-game ladder/ledge/swim/climb code; `stepMotion` remains the ground/air core.

- Load `locomotionModeConfig(settings)` from the optional `locomotion.*` keys (milli-units, ticks;
  see docs/gameplay-settings.md). Check `locomotionModeError(config)` for a readable message.
- Every tick the collision owner fills `LocomotionInput::env`: grounded/ceiling + ground normal,
  ledge (height above feet, normal), ladder (+ top/bottom flags, normal), water immersion depth,
  climbable wall normal, obstacle height/depth/normal. Normals point toward the character.
  The library never raycasts; missing reports mean "not present". Do not fabricate support.
- Call `stepLocomotion(config,state,input,tick,dt)` once per authoritative tick with constant dt;
  apply `result.velocity` through the owner's swept collision. React to `result.events`
  (LocoLedgeGrab, LocoVaultStart, LocoWaterEnter, ...) for effects/animation in presentation.
- Edges (`jumpPressed`, `grabPressed`, `dropPressed`) come from validated Commands. `vertical`
  drives ladders/walls/ledges/swim depth. Grab attaches ladders/walls; jump near a reported
  obstacle mantles/vaults; falling past a ledge auto-grabs unless `autoLedgeGrab` is false.
- Stamina: built-in counter (`climbStamina`, drain/regen per tick) or the owner's pool via
  `stepLocomotionWithStamina(..., spend)` where `spend(cost)` debits atomically.
- Persist with `genre::save/load` (state kind 20), hash with `locomotionHash`, and bind
  `locomotionConfigHash(config)` in the save envelope. Presentation IK climbing
  (docs/surface-locomotion.md) only reads this state; it is not the gameplay authority.
- Verify with `aiverse_library_genre_tests` (each mode trajectory, restore, rejection).
