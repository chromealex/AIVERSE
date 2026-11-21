# Races

A race is a capability of any project, not a genre: a map with a `"race"` block makes the map's cars race, in
whichever module hosts the cars. One data format, one simulation (`libraries/include/aiverselib/genre/RaceSession.h`,
data `genre/RaceSessionData.h`), one HUD (`engine/ui/RaceHud.h`), one chase camera (`engine/render/ChaseCamera.h`).
Skill: `skills/genres/aiverse-racing`.

## Data

Map (`"race"`, in the map's own units: metres in a metre map, cells in a grid map):

```json
"race": {
  "laps": 3,
  "checkpoints": [{"x": 30, "z": 20, "yaw": 0, "width": 16, "respawn": [30, 18]}, ...],
  "grid": [{"x": 28, "z": -12, "yaw": 0}, ...],
  "line": [[30, -60], [30, -55], ...]
}
```

- `checkpoints` in driving order, the last one is the finish line; `yaw` is the direction of travel in turns
  (0 = +z, 0.25 = +x), `width` the gate width; a car respawns at the gate (or `respawn`) facing `yaw`.
- `grid` (optional): one slot per racer; without it the cars start where the map puts them.
- `line`: closed racing line for the race bots (required when a racer is a bot).

Rules (`"race"`, optional): `countdownTicks` (3 s), `finishGraceTicks` (10 s), `timeoutTicks` (0 = none), `laps`
(overrides the map), `crashDamage` (false: crashes do not damage racing cars), `vehicles` (the generated `vehicles`
settings section: `wrongWayTicks`, `respawnWrongWayTicks`, `stuckTicks`, `stuckSpeedMilli`, race bot `bot*` knobs).

## Behaviour

- Racer `i` is the `i`-th car of the map and belongs to player `i + 1`. That player drives it (seated at the start,
  the doors stay shut until the race is over); a player with `"bot": true` in the map's `players`, or one without a
  body to drive with, races with the race bot along `line`.
- Countdown: every car holds its brakes. Then the cars race; gates count only in order (a shortcut past a gate gains
  nothing), laps, standings (place, lap, gate, distance to the next gate), wrong way, return to the last gate when
  stuck, driving the wrong way, wrecked or fallen off the world.
- The race ends when everybody finished, `finishGraceTicks` after the first finisher or `timeoutTicks` after the
  start; it decides the match: the winner is the team of place 1 (nobody finished: a draw).
- State is hashed and saved by the host (record kind 128 for the session). A map without `"race"` keeps its
  checksums and save layout.

## Hosts today (adapters, no rules of their own)

| Host | Cars | Adapter |
|---|---|---|
| Metre maps with box colliders (`modules/fps`) | typed map vehicles (`fps.vehicleTypes`) | `modules/fps/FpsRace.cpp` |
| Grid maps with heroes (`modules/rts`) | vehicles of rules `driving` | `Driving::loadRace` / `raceBefore` / `raceAfter` in `modules/rts/RtsDriving.cpp` |

Shared between the hosts: the race (above), the controls -> pedals mapping (`genre/VehicleDrive.h`: axes for keyboard /
gamepad, screen stick "tank" / "steer" for top-down and touch), the controller step and collision response
(`genre/VehicleControllers.h`), the HUD, the chase camera and the spline roads (`engine/roads`, drawn by both
clients). Still separate, recorded as debt in `docs/agent-handoffs/racing-2026-11-21.md`: getting in and out (the
metre-map host uses the library seats, the grid-map host its own enter / exit), the collision worlds themselves (box
colliders vs the map grid: host adapters by design) and the car's look (models with rolling / steering wheels are
drawn by the grid-map view; the metre-map client still draws boxes).

Samples: `templates/racing` (Oval Sprint: a metre map with walls, `templates/_tools/racegen.py`) and
`templates/road_race` (Road Circuit: a grid map, the circuit is a spline road — `roadNetwork` + road type `circuit` —
with the race on the same splines, `templates/_tools/roadracegen.py`; four Kenney CC0 trucks whose wheels roll and steer
through the view's `wheel` / `steer` procedurals on the `wheel-*` nodes).

Checks: `raceSession` (library), `raceMode` (`aiverse_fps_tests`), `driving_race_*` (`aiverse_tests`),
`aiverse_headless --project templates/racing --seed 3 --verify-restore-at 30 --verify-restore-ticks 600`.
