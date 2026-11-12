# Driving (the top-down hero at the wheel)

The hero of a top-down game (docs/topdown.md) gets into vehicles and drives them. Simulation:
`modules/rts/RtsDriving.{h,cpp}`; the vehicle dynamics are the wheeled controller of the vehicle-controllers
library (`libraries/include/aiverselib/genre/VehicleControllers.h`, docs/genre-packages.md § Vehicles).
Tests: `aiverse_tests driving_`.

## How it works

- **Vehicles** are ordinary units of the types listed in `driving.vehicles`, placed by the map or spawned later.
  Each gets a `VehicleState` (metres; `metersPerCell` converts) and `kUnitPossessed`, so RTS orders and the
  movement system leave it alone. A parked car holds its handbrake.
- **Getting in / out.** `kCmdTopDownEnter` (no units, no parameters; Lua / MCP `topdown_enter`) toggles: on foot the
  hero gets into the nearest free vehicle whose body is within `enterRange`; at the wheel he gets out when the car is
  slower than `exitSpeed` and a spot next to it is passable (driver's door = left side first, then right, rear,
  front). While driving the hero is off the map (`kUnitInCargo`: not hit, not targeted) and follows the car.
  The vehicle keeps its owner. `doorUntil` (tick) tells the view how long the driver's door stays open (`doorTime`).
- **Pedals** come from the hero's sticky `kCmdTopDown` input. `"controls": "tank"`: stick up the screen = throttle,
  down = brake, then reverse; sideways = steering (right on screen turns right). `"stick"`: the car turns toward the
  stick direction and accelerates (touch). The fire bit is the handbrake.
- **Collisions.** The body is three circles along the car (`length`, `width` in cells). Blocked cells push it out
  and take the speed into the wall (`crash.restitution`); impact speed above `crash.minSpeed` (m/s) damages the car
  by `crash.damage` per m/s. Car against car shares the push and exchanges the closing speed, both are damaged.
  Other ground units are run over above `runOver.minSpeed` (`runOver.damage` per m/s, credited to the driver);
  slower, they are pushed aside. Units that never move (`move.speed` 0: trees, posts) are solid like walls. Sub-steps of a quarter cell, so nothing tunnels.
- **Wrecks.** A destroyed vehicle throws its driver out on the spot with `wreckDamage`.
- **State / determinism.** `DrivingStore` world resource: hashed, saved after the top-down heroes, copied by
  rollback snapshots. Fixed maths only. Games without the block keep their checksums and save layout.

## Computer drivers

`Driving::setAutopilot(sim, vehicle, VehicleInput)` gives a vehicle nobody drives its pedals (throttle, brake,
steer, handbrake); `clearAutopilot` parks it again. It is simulation state (hashed, saved): call it from a system or
a script effect, never from the view. A hero who gets in takes over, and the autopilot input applies again when he
gets out. A parked car at rest is skipped by the controller, a car on autopilot never is. Traffic and traffic lights
are built on this by their own module; the city planner's `"city"` block of the map has the road graph.

## Levels: bridges over roads and tunnels

The map grid is one floor. A second floor is described on the map as a list of decks:

```json
"decks": [
  {"id": "ring_over_avenue", "width": 2, "path": [[40.5, 60], [40.5, 63, 5], [40.5, 71, 5], [40.5, 74]]},
  {"id": "hill_tunnel", "width": 2, "path": [[80, 20.5], [83, 20.5, -4], [95, 20.5, -4], [98, 20.5]]}
]
```

`path` is the centre line in cells (`[x, z]` or `[x, z, height in metres]`), `width` is the room between the
walls in cells. Both ends are open mouths at ground height.

- A car gets on when any part of it enters a mouth (the last 1.5 cells at either end) and gets off when the whole
  body has left through an end.
- On a deck the car is held between the deck's walls (railings, tunnel walls) and the grid is ignored: a tunnel
  runs under blocked cells, a bridge over water or over another road.
- A car deep on a deck (further than 1.5 cells from both ends) does not touch cars, people and posts on the
  ground or on another deck, and they do not touch it. Near the mouths everybody collides as usual.
- The hero cannot get out deep on a deck and cannot take a car that stands there.
- `Driving::deckOf`, `deckIndex(id)`, `height` (metres above the ground, for the view) and `setDeck` (a traffic
  system spawning a car inside a tunnel) are the code API. The deck of every car is part of the checksum and of saves.

What the map has to respect: the ground at both mouths is passable, and no other road crosses a deck closer than
two cells to its ends (a car crossing a mouth sideways would be taken onto the deck).

Not done yet for levels: people on foot (they still walk on the one-floor grid, so a bridge over a river works for
them, a tunnel and a bridge over a road do not), and the view does not lift a car to the deck height yet.

## rules.json

```json
"driving": {
  "metersPerCell": 4, "enterRange": 1, "exitSpeed": 3, "controls": "tank", "wreckDamage": 30, "doorTime": 0.5,
  "crash": {"minSpeed": 4, "damage": 5, "restitution": 0.2},
  "runOver": {"minSpeed": 3, "damage": 20},
  "vehicles": {
    "sedan": {"length": 1.1, "width": 0.5,
              "wheeled": {"massMilli": 1200000, "maxSteerMilli": 100}}
  }
}
```

`wheeled` takes the library's keys (torque curve, gears, grip curves, wheels, ...); omitted ones keep its defaults.

## Not done yet

Client: the enter key / button, a camera that pulls back with speed, layered car sprites (body, doors, wheels,
paint mask, reflection). Sim: passengers, computer drivers, per-surface grip, skid marks and crash events for
effects and sound.
