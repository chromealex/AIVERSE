---
name: aiverse-racing
description: Add or change a race in any AIVERSE project - map and rules "race" (grid, countdown, ordered checkpoints, laps, standings, wrong way, respawn, race bots, shared HUD and chase camera) - and check it. No genre or module switch.
---

# races

A race is a capability of any project with cars, never a genre mode. Format and behaviour: `docs/races.md`.
Simulation: `engine/mechanics/RaceSession.h` (data: `mechanics/RaceSessionData.h`; dynamics and the race
bot: `skills/gameplay/aiverse-vehicle-controllers`). Presentation: `engine/ui/RaceHud.h`, `engine/render/CameraBehavior.h`.

1. The project needs cars its host already steps: typed map vehicles (`actor.vehicleTypes` + map `vehicles` with
   `"type"`) on a metre map, or rules `driving` vehicles on a grid map. Racer `i` = the `i`-th car = player `i + 1`.
2. Map `"race"` in the map's own units: `checkpoints` in driving order (`x`, `z`, `yaw` = direction of travel in
   turns, `width`; the last one is the finish), optional `grid` (one slot per racer), `line` (closed racing line;
   required when a racer is a bot). Players with `"bot": true` race with the race bot.
3. Rules `"race"` (optional): `countdownTicks`, `finishGraceTicks`, `timeoutTicks`, `laps`, `crashDamage`, `vehicles`
   (generated settings: `wrongWayTicks`, `respawnWrongWayTicks`, `stuckTicks`, `stuckSpeedMilli`, `bot*`).
4. A ready oval for a metre map: `python3 tools/templates/racegen.py --out <game> [--racers N --bots B --straight M
   --radius M --width M --laps N]` (walls, cars, the map race block, the car and the knobs); sample `~/.aiverse/packages/templates/racing`.
5. The race adapter is shared engine code: `vehicles::VehicleRace` (`engine/vehicles/VehicleRace.h`; state
   `vehicles::RaceState`). A new host for cars runs its cars on the vehicle system (`engine/vehicles`,
   `vehicles::VehicleHost`) and implements only `vehicles::RaceHost` (grid placement, seating a player / bot, the
   racer's own pedals, respawn request and sync, the match result): `VehicleRace::read` at setup, `start`, per tick
   `pedals` -> `vehicles::stepVehicles` -> `after`, `view` for the HUD, `hash` / `io`. No rules of its own
   (`docs/races.md` § One adapter, two hosts).
6. Check: `aiverse_headless --project <game> --seed 3 --verify-restore-at 30 --verify-restore-ticks 600` ends with a
   winner; `aiverse_fps_tests` (`raceMode`), `aiverse_tests driving_race_`, the vehicle library tests (`raceSession`);
   a screenshot at the wheel under Xvfb. Tune bot pace until bots finish without respawns.

Not available yet: car-to-car collisions on metre maps, pits, penalties, flags, ghosts, a track editor.

Runtime library: none of its own (hosts + `AIVERSE::lib_vehicle_controllers`).
