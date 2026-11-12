---
name: aiverse-physical-interactions
description: Reuse AIVERSE deterministic physical interaction policies (pick up/carry/throw with arc preview, push/pull, ropes, doors with locks and auto-reverse, moving platforms with rider carry, breakable objects with debris requests) when building native C++ puzzle, immersive-sim, platformer or survival games instead of writing per-game physics glue.
---

# physical-interactions

Use when a native/host owner needs gameplay-authoritative object interaction: carrying and throwing props,
pushing crates (free or on a grid), hanging/swinging ropes, doors and gates, elevators/moving platforms,
destructible objects. Not for action menus or built-in engine commands (Smart/PickItem/UseItem/ToggleLock):
those are `object-interactions`. Not a rigid-body engine: `engine/physics` (Box3D) stays optional and
presentation-only for these mechanics.

1. Read `docs/genre-packages.md` (physical-interactions section) and the catalog entry. Headers under
   `libraries/include/aiverselib/genre/`: `PhysicalInteractions.h` (umbrella: `Carry.h`, `Push.h`, `Ropes.h`,
   `Doors.h`, `Platforms.h`, `Breakables.h`, shared `PhysicalCommon.h` / `InteractionConfig.h`),
   `InteractionConfigured.h`, `InteractionData.h`, `InteractionPersistence.h`.
2. Determinism/network model: these Fixed-math, integer-tick policies ARE the authoritative state. Run them on
   the authority (or in lockstep on every peer) inside the command handler/system, never from presentation.
   Collision is the owner's: pass `WorldCollision(spatialWorld)` (engine/world) or any object with
   `sweep(start,delta,half,ignore)` and `overlapping(box,ignore)`. Box3D may only animate debris from
   `DebrisRequest` (use its `seed`); never copy Box3D poses back into these states.
3. Data: scalar knobs in the optional generated `interaction` section of `libraries/spec/gameplay.json`
   (`readGameplaySettings`, `interactionSettingsErrors`, `interactionConfig`). Object definitions:
   `readInteractionDefinitions(json)` (example `examples/libraries/genre/interactables.json`); errors are
   `JsonError("interaction.doors[0].openTicks: must be in 1..3600")`. Edit the spec and run
   `python3 tools/libraries/generate_gameplay.py`; never hand-edit generated files.
4. Per authoritative tick call each used step exactly once with strictly increasing ticks (repeated/backwards
   ticks or invalid input return nullopt and leave state untouched):
   - `stepCarry(config, carryables, state, tick, dt, CarryInput{carriers, freeObjectPoses}, collision)`:
     pickUp/drop/charge level per carrier; read `CarrierStatus::speedMilli` (carry penalty) and held/flight
     positions from the state; `previewThrow` + `throwVelocity` + `carryChargeMilli` draw the exact arc.
   - `stepPush(config, pushables, state, tick, dt, commands, collision)` after `addPushable`.
   - `stepRopes(config, ropeDefs, state, tick, dt, RopeInput{bodyPoses, attach/detach})` after `makeRope`;
     apply `RopeStatus::pullA/pullB` to Body ends in your own controller; Load ends are moved by the rope.
   - `stepDoors(config, doorDefs, state, tick, commands, collision)` after `addDoor`; update the door collider
     from `DoorStatus::bounds`. Give doors a collision view of bodies (not the static frame).
   - `stepPlatforms(config, platformDefs, state, tick, dt, commands, riderContacts)` after `addPlatform`; for each
     `RiderCarry` add `delta` to the rider (swept) BEFORE its `stepLocomotion` move, keep `env.grounded` from the
     same contact, optionally add `velocity` once when the rider leaves (momentum inheritance).
   - `stepBreakables(breakableDefs, state, tick, damage)` after `addBreakable`; damage ids increase per object.
   Consume `InteractionEvent`s (kind, object, actor, value = reason/impulse/tension/stage, collider, position).
5. Persist each state with `genre::save/load` + `storeBytes/loadBytes` in its own NativeState key (record kinds
   80..85 used, 86..87 reserved), fold `interactionHash(state)` into the checksum, bind `interactionConfigHash` and
   `interactionDefinitionsHash` in the save envelope. Call `releaseCarried` when the owner destroys a prop.
6. Example: `examples/native/libraries/GenreConsumer.cpp` (`physicalInteractionConsumerExample`). Tests:
   `tests/libraries/genre_tests.cpp` (`physicalInteractions`). Run `python3 tools/libraries/catalog.py check`,
   `generate_gameplay.py --check`, `tests/libraries/run.py`, `native_consumer.py`.
   Not implied: rigid-body stacking, rotation of props/platforms, rope-world collision, crushing, vehicle physics.

Build selection: `AIVERSE::lib_physical_interactions` (depends on `AIVERSE::lib_configuration`, `aiverse_engine`,
`aiverse_world`); native OBJECT modules call `aiverse_native_use_libraries(target physical-interactions)`. Only this
header library and its declared dependencies are selected. See `skills/catalog.json` for dependency routing.
