# City traffic (computer-driven cars, traffic lights, pedestrians)

A top-down city game (docs/topdown.md, docs/driving.md) gets living streets from data only: cars driven by the
computer keep their lanes, turn at junctions and stop at red lights; traffic light props show the signals;
pedestrians walk the sidewalks, wait at the kerb and cross at the junctions on "walk", and run away from gunfire.
Simulation: `modules/rts/RtsTraffic.{h,cpp}`; the reusable kernels (road network, lanes, signal phases, the bot
driver's pedals, sidewalk paths) are header-only in `libraries/include/aiverselib/genre/Traffic.h` and can be used
by a game's native module without the RTS module. Tests: `aiverse_tests traffic_`.

## How it works

- **Road network from the map's tiles.** At setup the map's `tiles.rows` characters listed in `roads.tiles` are road
  cells. A road cell whose horizontal and vertical runs are both longer than `roads.maxWidth` is a junction cell, one
  with only a long horizontal (vertical) run belongs to a road along x (z); connected cells form junction boxes and
  road pieces. Each piece has `width / 2 / laneWidth - parkingLanes` lanes per direction next to its centre line
  (the outer `parkingLanes` per side are left to parked cars). Right-hand traffic unless `"side": "left"`. A map can
  switch traffic off with `"traffic": false`.
- **Signals.** A junction with three or more driven arms is signalised. Its phases: traffic along x green, yellow,
  all red, then the same for z (`signals.green / yellow / allRed`, seconds). Junction offsets are `"random"`
  (fixed per junction position), `"wave"` (a green wave along x + z) or `"none"`. With `signals.lights` the engine
  places one light prop per arm at the right kerb before the stop line, facing the traffic, and switches its type
  between the green / yellow / red types (give each a `view.light` glow of its colour; no `upgraded` event).
- **Cars.** Traffic cars are ordinary `driving` vehicles (types listed in `cars.types` must be in
  `driving.vehicles`) without a driver: this module is their driver. Every tick it keeps a route of lane points and
  turn curves (quadratic Bezier through the junction; the exit is chosen by `straight` / `turn` weights, U-turn at
  dead ends), steers by pure pursuit (projection on the route plus a speed-dependent look-ahead) and sets the target
  speed from its cruise speed (`speed` range), turns (`turnSpeed`), the next stop line (red: stop; yellow: stop if it
  comfortably can at `decel`) and the nearest body ahead on the road along its path (cars, people, the hero, parked
  cars, wrecks; people get a wider berth). The pedals go into the same vehicle controller the hero drives with, so
  traffic cars crash, push and get pushed. Deadlocks: two cars that wait for each other let the lower entity index
  go; a car stuck behind a parked car / wreck on a straight drives round it over the centre line; a car pushed off its
  route finds the nearest lane again. Cars appear out of sight around the heroes (`spawnMin`..`spawnMax` cells; the
  first tick fills the area) up to `max` and disappear beyond `despawn` (or when stuck / lost out of sight).
- **Islands.** A junction box with blocked, non-road cells strictly inside it (a monument, the middle of a
  roundabout) is driven round: the route follows the lane between the island and the edge of the box, the island on
  the driver's side (anticlockwise on the map for right-hand traffic), and leaves at the chosen arm. The lane is
  tight, so nobody joins a long vehicle (over 2 cells) on it and a long vehicle waits until it is empty.
- **Stop lines.** Cars stop with their nose 0.2 cells before the junction, or before the pedestrian crossing strip
  when the arm has one; the light props stand there too.
- **Buses** (`buses`). Bus types drive the outermost driving lane at `speed` and call at the stops on their side of
  the road: map objects of the `stops` types (shelters, signs) within 3 cells of a driven road piece, and / or one
  stop per side in the middle of every `autoStops`-th road piece of 8 cells or more. At a stop the bus stands for
  `dwell` seconds; up to `passengers` people standing about within 2 cells get on (they leave the world) and up to
  `passengers` get off onto the sidewalk and walk away. The traffic behind waits.
- **Emergency vehicles** (`emergency`). Every `interval` seconds one is called out (up to `max` at a time): it
  drives at `speed` near the centre line, slows for red lights but does not stop, and pushes through vehicles that
  block it for 2 s (never through people). Within `yield` cells ahead of and beside it the others react: whoever
  drives the same way ahead of it keeps going on the kerb side of its lane and through red lights (a queue that
  stopped would trap it); everybody else keeps right and waits at the next junction instead of entering it.
  Sirens and beacons are presentation: give the types a `view.light` with `"pattern": "beacon"` and ask
  `game.traffic_kind(h)` from a view script for the sound.
- **Long vehicles** (over 1.6 cells) take bends at 2.5 m/s at most: slower than `driving.runOver.minSpeed`, so a
  swinging tail pushes people aside instead of running them over. A car creeping past a person it stopped for does
  the same. A vehicle wedged at full throttle with nothing ahead backs off for 1.5 s and tries again.
- **Hijacking.** When a hero gets into a traffic car (`kCmdTopDownEnter`) it stops being traffic; with
  `pedestrians.driver` its driver is thrown out on the far side and runs.
- **Pedestrians.** People of `pedestrians.types` (placed by the map or spawned around the heroes up to `max`)
  walk breadth-first paths (`route` cells long) over the sidewalk cells (`pedestrians.walk` characters) and the
  crossing strips: the first row of a road piece next to a junction, when sidewalks touch it on both sides. They
  get one waypoint at a time (a blocked walker never re-paths across the road), keep a personal offset from the
  sidewalk centre line (`side`), pause between walks (`pause`), and at a signalised junction wait at the kerb until
  the traffic across their way is green and at least 1.5 s of its walk window (`signals.walk`) are left. A person
  who is hurt, sees a shot, a hit or a death within `panic.radius`, or has a fast car (driven by a hero) coming at
  him panics: he runs (`panic.speed` multiplier) toward the sidewalk cell farthest from the threat for
  `panic.seconds`, signals ignored. Orders from scripts or the computer player end the walk (the script owns it).
- **Big cities.** Only the surroundings of the heroes are simulated, and no per-tick cost grows with the map:
  cars, buses and people are spawned by sampling cells of the square around a hero and removed beyond `despawn`;
  light props exist only at junctions within `signals.radius` of a hero (signal phases are a pure function of the
  tick, so `game.traffic_light` still answers anywhere); pedestrian paths are searched in a window of
  `route max + 8` cells; parked vehicles at rest sleep (docs/driving.md), so hundreds of them cost nothing until
  something hits one. Setup is linear in the map cells (network, masks: about 20 bytes per cell). Measured on a
  384 x 384 city with 690 parked cars, 18 cars, 2 buses, 40 people: 0.3 ms per tick, 0.04 ms of it this module.
- **State / determinism.** `TrafficStore` world resource: hashed, saved after the vehicles, copied by rollback
  snapshots. Fixed maths, entity-index order, randomness from the simulation RNG only. Games without the block keep
  their checksums and save layout.

## The living city: incidents and services

`modules/rts/RtsTrafficCity.cpp`. Things happen, and the service that answers that kind of thing sends a vehicle.
Tests: `aiverse_tests traffic_city`.

| incident | raised when | answered by (`answers`) | what the service does |
|---|---|---|---|
| `injured` | a person (pedestrian type, thrown-out driver, crew member) is killed: a `body` object is left | ambulance | the crew walks to the body, works, takes it along; the person is treated for `treat` seconds in the hospital the ambulance came from and then walks out of it and goes on |
| `crime` | a hero shoots, kills a person or blows up a car **and somebody within `witness` cells saw it**; scripts report robberies with `game.traffic_report` | police | officers get out and attack-move to the scene; while the offender is within 15 cells they go for him (up to `maxWork` seconds) |
| `fire` | a vehicle is destroyed: a `fire` object burns at the wreck for `burn` seconds and hurts what stands within `fireRadius` (`fireDamage` per second; a burning car next to another one can set it off too) | fire service | stands at the fire for `work` seconds and puts it out |
| `wreck` | a destroyed vehicle leaves a `wreck` object; a car a hero left on a driving lane or in a junction for `abandon` seconds | tow truck | waits until no fire burns within 3 cells, then loads it; it is put next to the nearest `lots` object (rows of four) or simply taken away |

- **Dispatch.** Every half second each open incident gets a free vehicle of a service that answers it (`max` on
  call per service). It drives out of the nearest station object (`stations` types within `range` cells of the
  incident, on the station's side of the road); with no station near it arrives from out of sight. It is a traffic
  car with an address: at every junction it takes the exit on the shortest way (in road pieces) to it, with a siren
  (`"siren": true`: the emergency rules above) or as ordinary traffic (tow trucks). Vehicles on call are not removed
  with the distance to the heroes. A call that takes longer than `timeout` goes back to the queue.
- **At the scene.** The vehicle stands (no siren any more, traffic passes). With a `crew` type, `crewCount` people
  get out on the kerb side and walk to the spot; the work starts when one of them is there. Afterwards the crew
  walks back and the vehicle returns to its station and drives in (or leaves as ordinary traffic). A vehicle that is
  wrecked or stolen on call loses its crew and the call is given to the next one.
- **One thing leads to another.** A thrown-out driver's car theft, shots and killings bring the police only when
  there was a witness; officers and medics who die become `injured` incidents themselves; fires damage people (who
  panic) and cars (which may blow up in turn); wrecks block lanes until the tow truck comes, and traffic drives
  round them; a body nobody collects disappears after `bodyTime`, a wreck after `wreckTime`.
- Scripts: `game.traffic_report(kind, x, z [, offender])` (a robbery, a grenade fire: `"fire"` places the fire
  object) and `game.traffic_incidents()` (HUD markers, missions).

```json
"services": {
  "ambulance": {"answers": "injured", "types": ["ambulance"], "stations": ["hospital"], "crew": "medic", "crewCount": 2, "work": 4},
  "police":    {"answers": "crime", "types": ["police"], "stations": ["precinct"], "crew": "cop", "work": 8, "maxWork": 60, "owner": 2, "max": 2},
  "fire":      {"answers": "fire", "types": ["fire_engine"], "stations": ["fire_station"], "work": 6},
  "tow":       {"answers": "wreck", "types": ["tow_truck"], "work": 4, "speed": [7, 9], "siren": false}
},
"incidents": {"body": "ped_down", "bodyTime": 90, "treat": 60, "wreck": "car_wreck", "wreckTime": 300,
              "fire": "fire", "burn": 45, "fireDamage": 6, "fireRadius": 1.2, "abandon": 40, "witness": 7,
              "lots": ["tow_lot"], "crimeTime": 60, "timeout": 150}
```

`body`, `wreck`, `fire` and the station / lot objects are ordinary prop types (`move.speed` 0, `invulnerable`); give
the fire a `view.light` and particles. Crew types are ordinary units: officers need `weapons` and an `owner` whose
team is hostile to the heroes' to fight them. Service keys default to `range` 90, `crewCount` 2, `work` 5,
`maxWork` 60, `max` 1, `speed` [12, 15].

## rules.json

```json
"traffic": {
  "roads": {"tiles": "ahv", "laneWidth": 1, "parkingLanes": 1, "maxWidth": 6, "side": "right"},
  "cars": {"types": {"sedan": 5, "taxi": 3, "van": 1}, "max": 18, "owner": 0, "speed": [7, 11], "turnSpeed": 4.5,
           "decel": 5, "gap": 0.5, "straight": 3, "turn": 2, "spawnMin": 14, "spawnMax": 26, "despawn": 34},
  "buses": {"types": ["city_bus"], "max": 2, "speed": [6, 8], "stops": ["bus_shelter"], "autoStops": 0,
            "dwell": [3, 6], "passengers": 2},
  "emergency": {"types": {"ambulance": 1, "fire_engine": 1, "police": 2}, "max": 1, "interval": [20, 60],
                "speed": [13, 16], "yield": 7},
  "signals": {"green": 9, "yellow": 3, "allRed": 1, "walk": 6, "offset": "random", "radius": 38,
              "lights": {"green": "light_green", "yellow": "light_yellow", "red": "light_red"}, "place": true},
  "pedestrians": {"walk": "ps", "types": {"ped": 3, "ped_runner": 1}, "max": 40, "owner": 2,
                  "spawnMin": 12, "spawnMax": 22, "despawn": 30, "route": [8, 30], "pause": [1, 5], "side": 0.3,
                  "panic": {"radius": 6, "seconds": 8, "speed": 1.8}, "driver": "ped"}
}
```

Units: cells for distances, m/s for speeds (`driving.metersPerCell` converts), seconds for times. Type lists are
`["id", ...]`, `{"id": weight}` or `[{"type": "id", "weight": n}]`. Bus and emergency types must be `driving` vehicles like the cars; `signals.radius` defaults to `cars.despawn` + 4.
`"signals": false` turns the signals off;
`"enabled": false` the whole block. Loader errors name the key (`traffic.cars.types: 'ped' is not in
driving.vehicles`).

Light prop types are ordinary decoration types, e.g. the traffic light model with a glow:

```json
"light_red": {"health": {"max": 100000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.067}, "sight": 0,
              "view": {"model": "models/props/07_traffic_light.glb", "scale": 0.25,
                       "light": {"color": [1, 0.12, 0.08], "radius": 1.4, "intensity": 2.2, "height": 0.85, "halo": 0.32}}}
```

## Lua

`game.traffic_light(x, z [, "x"|"z"])` → `"green"`, `"yellow"`, `"red"` or nil: the signal of the junction at or next
to the point (within 3 cells) for traffic along that axis (default: the axis of the road the point is on).

`game.traffic_kind(h)` → `"car"`, `"bus"`, `"emergency"` for a vehicle the traffic drives, nil for anything else
(parked cars, the hero's car). Works in view scripts (sirens, HUD markers).

## C++ library (`aiverselib/genre/Traffic.h`)

| function | contract |
|---|---|
| `buildTrafficNet(w, h, roadMask, cfg)` | pieces, junction boxes, lanes, arms, signalised junctions; row-major deterministic |
| `trafficLaneAcross / trafficLanePoint / trafficPieceStart / trafficPieceEnd` | lane geometry by direction (+1 / -1 along the piece) and lane index |
| `trafficLight(timing, offset, tick, axis)`, `trafficWalk(..., roadAxis, margin)`, `trafficUntilGreen` | signal phases |
| `trafficDriverInput(params, yaw, speed, pos, target, targetSpeed)` | `VehicleInput` of the bot driver (handbrake at a standstill with target 0) |
| `trafficPursuit(pos, polyline, ahead)`, `trafficCurve`, `trafficStopSpeed` | pure pursuit point, junction curves, braking envelope |
| `trafficWalkField / trafficWalkPath` | breadth-first sidewalk distances and paths |

## Not done yet

A persistent population (homes, jobs, shifts, schools, shops that open with their keeper), money, gangs and
robberies, wanted levels with arrest and jail, the hero as a casualty (hospital) — planned on top of the incidents.
A knocked-down person is a death for the engine (`unit_died` fires) and comes back as a new unit of the same type.
Lane changes and overtaking on multi-lane roads (cars keep their lane; only blocked cars swerve), bus lines with fixed
routes and people waiting at the stops, emergency call-outs that answer events (police after shots, an ambulance for
the wounded) and chases, protected left-turn phases, pedestrians entering buildings, crossings in the middle of a
block, traffic on non-tile maps (road graph from `roads` / splines). Long buses on a one-cell lane round a small
island get through but slowly; give such junctions a wider ring or keep `parkingLanes` 0 there.
