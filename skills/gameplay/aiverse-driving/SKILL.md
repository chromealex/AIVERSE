---
name: aiverse-driving
description: Let directly controlled characters of any AIVERSE project get into and drive vehicles - rules driving (vehicles, controls, crashes, run-over), per-vehicle handling from a table, steering wheels in the view, bridges and tunnels as decks, computer drivers through the autopilot - and check it.
---

# driving

Behaviour and format: `docs/driving.md`. Simulation: the one vehicle system `engine/vehicles/Vehicles.*` + `VehicleRace.*` (components `vehicles::VehicleDrive` / `VehicleOccupant`, `stepVehicles` over a host `VehicleHost`); grid-map host `engine/game/Driving.*`, metre-map host `engine/actor/ActorVehicles.cpp` (dynamics: `engine/mechanics/VehicleControllers.h`, `skills/gameplay/aiverse-vehicle-controllers`). Client camera: `apps/client/ClientTopDown.cpp`. Tests: `aiverse_tests driving_`.

1. Needs directly controlled characters (rules `topDown`, legacy block name; `docs/topdown.md`). Vehicle logic lives
   only in `engine/vehicles` (stage B of `docs/no-genre-split-audit.md`): a host implements `vehicles::VehicleHost`
   (ground, sweep, driver pedals, placement, damage) and `vehicles::RaceHost` (race seats) and never steps
   controllers itself. Change behaviour in the shared system, not in `RtsDriving` / `FpsVehicles`.
2. Rules `driving`: `metersPerCell`, `enterRange`, `exitSpeed`, `controls` (`tank` for keyboard / gamepad, `stick` for touch), `crash`, `runOver`, `wreckDamage`, `doorTime`, and `vehicles.<type>` with `length`, `width` (cells) and the library's `wheeled` keys. Vehicle types are ordinary units placed by the map or spawned.
3. Handling per vehicle: `python3 tools/citygen/vehicle_handling.py <project>` writes `wheeled` from its table and the wheel positions from `wheel_<axle>_<side>` model nodes, plus the `steer` / `wheel` procedurals. Library defaults are a road car; a bus or a lorry needs its own numbers.
4. Wheels in the view: procedural `"steer"` before `"wheel"` on the front wheel nodes; mirrored left wheels need the opposite spin axis.
5. Levels: map `decks` (bridges over roads, tunnels: `path` with heights in metres, `width`); keep both mouths passable and other roads two cells away from them.
6. Computer drivers: `vehicles::setAutopilot` / `clearAutopilot` (grid map: `Driving::setAutopilot`) from a system or script effect (simulation state). City traffic builds on it (`skills/gameplay/aiverse-city-traffic`).
7. Check: headless with rules `"ai": {"controller": "scripted"}` steers the hero too (measure turn rate, top speed, reverse); same seed, same checksum; maps without vehicles keep theirs; screenshot at the wheel (camera follows the car). Lamps and tyre marks: `skills/visuals/aiverse-vehicle-fx`.

Runtime library: none (engine module). Workflow over existing engine code.
