---
name: aiverse-vehicle-controllers
description: Reuse AIVERSE vehicle policies (wheeled arcade-sim with torque curve, gears, per-wheel traction, handbrake and drift; tracked, hover and flight controllers; damage zones; driver/passenger/gunner seats with enter/exit; race checkpoints, laps, positions, wrong-way and respawn; race bot) when building native racing, vehicle-combat, transport or open-world games.
---

# vehicle-controllers

Use when a native/host owner needs drivable vehicles or races: car/kart/truck handling, tanks, hovercraft, arcade
planes, vehicle damage that degrades handling, getting in and out of seats, lap races with positions, AI racers.
Not a rigid-body simulator: no suspension geometry, tyre temperature or Box3D authority. Mounted weapons use
`weapon-policies`; on-foot movement uses `character-locomotion`; generic objectives stay in `match-rules`.

1. Read `docs/genre-packages.md` (vehicle-controllers: determinism/network model) and the catalog entry. Inspect
   `libraries/include/aiverselib/genre/Vehicle*.h`, `RaceCourse.h` and `bots/RaceBot.h` for exact behaviour.
2. Authority split. Authoritative (save, hash, identical on every peer): `VehicleState`, `VehicleDamageState`,
   `VehicleSeatState`, `RaceState`, `RaceBotState`. Owner: collision/ground queries reported as `VehicleContacts`
   (grounded, ground height/normal, surface grip, optional per-wheel contacts), collision resolution after the step
   (`vehicleResolveCollision(state, position, normal, restitution)` returns the impulse for damage), teleports on
   respawn (`respawnVehicle`). A Box3D world may mirror poses for presentation but is never read back.
3. Data: `readVehicleDefinitions(json)` (`genre/VehicleData.h`, example
   `examples/libraries/genre/vehicles-sports/vehicles.json`): `vehicles` (kind wheeled|tracked|hover|flight with
   its parameter object, `zones`, `seats`) and `tracks` (`checkpoints`, `grid`, racing `line`). Milli-units, rpm,
   ticks; errors `vehicles.vehicles[0].wheeled.gearsMilli[1]: ...`. Scalars: optional generated `vehicles` settings
   section (`vehicleRaceConfig`, `vehicleSeatLimits`, `vehicleRaceLaps`, `bots::raceBotConfig`, `vehicleSettingsErrors`).
4. Per authoritative tick, ascending vehicle id: seats (`stepVehicleSeats` with the owner's `clear(point, actor)`
   exit query) -> driver input (player or `bots::stepRaceBot`, only for `vehicleDriver`) -> `stepVehicle(def, state,
   tick, dt, input, contacts, vehiclePerformance(damageDef, damage))` -> owner sweep/collisions -> `stepVehicleDamage`
   with id-ordered `VehicleDamageInput` (zone 0 = locate by `vehicleLocalPoint`) -> `stepRace` with every racer's
   position/velocity; apply `report.respawns`. Strictly increasing ticks; invalid input returns nullopt untouched.
5. Races: the last checkpoint is the finish line (grid just after it); crossings use match-rules `crossesGate` +
   `stepRoute`; standings rank (place, lap, checkpoint, distance to next, earlier gate, id); wrong way after
   `wrongWayTicks` backwards, auto respawn after `respawnWrongWayTicks` / `stuckTicks`.
6. Race bot: `makeRaceLine(points)` once (closed loop), `stepRaceBot(config, line, state, tick, view)` per bot with
   its place and visible opponents -> `VehicleInput`. Knobs: corner accel / brake decel (braking points), lane
   offset/overtake distance, follow distance, `skillMilli`, `rubberBandMilli` (straight-line pace only).
7. Persist kinds 88 VehicleState, 89 VehicleDamageState, 90 VehicleSeatState, 91 RaceState, 94 RaceBotState
   (`genre/VehiclePersistence.h`; 88..95 shared with sports-rules) in dedicated NativeState keys; fold
   `vehicleHash(state)` into the checksum and bind `vehicleDefinitionHash`, `raceCourseHash`, `bots::raceLineHash`.
8. Examples: `examples/native/libraries/VehiclesSportsConsumer.cpp` and the executable 4-car race in
   `examples/libraries/genre/vehicles-sports`. Run `catalog.py check`, `generate_gameplay.py --check`,
   `aiverse_library_vehicles_sports_tests` and the `library_vehicles_sports_example` CTest.

Build selection: `AIVERSE::lib_vehicle_controllers` (depends on `match-rules`, `configuration`, `aiverse_engine`);
native OBJECT modules call `aiverse_native_use_libraries(target vehicle-controllers)`. See `skills/catalog.json`.
