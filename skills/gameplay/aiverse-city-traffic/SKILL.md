---
name: aiverse-city-traffic
description: Set up and extend road traffic in any AIVERSE project - lane cars, buses, emergency vehicles, signals and light props, pedestrians, incidents and services, population with homes and shifts, gangs, spline-road traffic - and reuse the city-traffic C++ kernels (road net, signals, driver pedals, sidewalk paths, wide routes and detours) from native modules.
---

# city-traffic

Behaviour and format: `docs/traffic.md`. Simulation: `engine/game/Traffic.*`, `engine/game/TrafficCity.cpp` (incidents, services), `engine/game/TrafficPopulation.cpp` / `engine/game/TrafficCommute.cpp` (population), `engine/game/TrafficGangs.cpp`, `engine/game/TrafficRoads.cpp` (spline roads). Kernels: `engine/mechanics/Traffic.h` and `engine/mechanics/traffic/TrafficRoutes.h` (`AIVERSE::lib_city_traffic`). Tests: `aiverse_tests traffic_`, `tests/libraries/city_tests.cpp`.

1. Prerequisites: vehicles in `driving.vehicles` (`skills/gameplay/aiverse-driving`), roads as tiles listed in `traffic.roads.tiles` or a spline `roadNetwork` (`skills/world/aiverse-roads`, `"roads": {"network": true}`).
2. Rules `traffic`: `cars`, `buses` (stop types or bays), `emergency`, `signals` (+ `lights` prop types with `view.light`), `pedestrians` (walk tiles, `panic`). Loader errors name the key; fix them before anything else.
3. Living city: `services` (ambulance / police / fire / tow: `answers`, `stations`, `max`, `range`; `custody` / `carry` flags), `population` (place kinds with `lives`, `jobs`, `shifts`; map `places`), `gangs`. Add one block at a time and run `aiverse_headless --project <dir> --map maps/<map>.json --seed 3`.
4. Lua (UI / view only in games that keep simulation in C++): `game.traffic_light`, `game.traffic_kind`, `game.traffic_gang`, `game.traffic_citizen`.
5. Native modules: build a net with `buildTrafficNet` (or read the engine's), signals with `trafficLight` / `trafficWalk`, pedals with `trafficDriverInput`, sidewalks with `trafficWalkRoute`; large vehicles: `trafficWideExit` (prefers carriageways of `minWidth`, first and last piece always allowed), blockages: `trafficLocalDetour` (bounded lattice, then the normal collision checks). Give cars pedals through `vehicles::setAutopilot` (engine/vehicles; grid map: `Driving::setAutopilot`), never by moving them.
6. Checks: same seed, same checksum; maps without `traffic` keep theirs; screenshots of a junction (lights, stop lines) and of a crossing on "walk".

Host: the traffic simulation is a system of `engine/game` (GameCore, the one host for every project).

Game-specific type ids or variable names never go into `engine/game`: put them in rules data or the game's `native/`.

## Population decision budget

Optional rules `traffic.population.updatePeriodTicks` (0 = two seconds at simulation tick rate, 1..36000 explicit) and `maxUpdatesPerTick` (0 = automatic, 1..200000 explicit) tune the existing recurring population scan. Use `simulation_budget_plan` with count, periodTicks and maxVisitsPerTick to see actual maximum revisit latency. The cap may intentionally miss the desired period; do not interpret it as a guaranteed two-second reaction. No additional mutable owner or save fields. Mandatory physics and live movement are unchanged. Keep actual project rules in data; no genre-specific host or duplicate city implementation. Native and MCP acceptance pending.
