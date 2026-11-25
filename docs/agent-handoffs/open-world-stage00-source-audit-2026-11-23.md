# Open World Framework — stage 00: external-project capability audit and scope
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Code commit: `dbb1956bc33fc04524262094e36acd11e13c8e91` (`docs/open-world-framework.md`)
Area: `W1` / open-world region lifecycle (existing partial, **not** claimed complete)
External source: `chromealex/gta`, `main@5dc986b78d30174a0409b3dd64f37d89ff06cf9d` (2026-11-17)
AIVERSE source baseline: `main@aec89ef435aba457ebeec24ef196f62a2fbb611d` (2026-11-23)

## What happened in this stage

Inspected the two repositories through their current GitHub file content. Classified the external game's source into (a) reusable C++ native algorithms, (b) game-specific data/rules, (c) legacy Lua runtime, and (d) client-only/private-engine adapters. Produced a shared-capability roadmap with 16 work packages, dependencies and concrete acceptance tests. No engine runtime files, tests, templates, or gameplay assets were modified. No `main` merge was requested or performed.

## Already present in AIVERSE — don't recreate

- `engine/world/{ChunkStreamer,AsyncChunkStream,CollisionStream}.*`, `docs/large-worlds.md`: residency and asynchronous terrain/mesh/collision; not a general persisted dynamic-ECS region lifecycle.
- `engine/nav/NavMesh.h`: tiled/multilevel navmesh; streaming and dirty dynamic-world integration remain.
- `engine/roads/`, `engine/game/Traffic*`: road lanes, signals, citizens, businesses, commutes, police/fire/ambulance/tow incidents; `docs/traffic.md`.
- `engine/vehicles/`, `engine/mechanics/VehicleControllers.h`: unified driver/occupant and controller framework; `docs/driving.md`.
- `engine/animation/`, VIVID, PHYSIS, NEREID, PRISMA, SpectraFX, narrative and mission policies.

## External game source inventory (reusable policy vs project gameplay)

| Subject | Actual external evidence (repo-relative) | What is already written | Shared-system decision | Cautions |
|---|---|---|---|---|
| Arrest and custody | `project/native/police/{README.md,Policy.h,Response.inc,police_tests.cpp}` | Weapon eligibility, escort, vehicle boarding, precinct delivery, fee; test documentation includes save/restore | Extend AIVERSE incident/vehicle/actor action APIs; reuse generic eligibility/escort state machine | Installs in **private engine snapshot**, not listed as standalone `nativeModules` entry in checked rules. Not proof of current AIVERSE compatibility |
| Heat/wanted | `project/scripts/wanted.lua`, referenced in `project/rules.json` | Five-star heat thresholds, crime points, cooling, fines | Replace authoritative Lua with generic C++ policy and normal commands/events; leave game balance in external rules | Explicit legacy simulation; NOT ported native policy |
| Crime, commerce and city interactions | `project/native/citylife/{Module.cpp,README.md}` | Talks, dealer, jobs, robbery/getaway, locks, drops, staff and customer visits | Compare and extract *only* generic dispatch/interactions; game content remains external | CMake subdir exists but `kingston_citylife` not in the inspected `rules/native-modules.json` array; runtime activation must be established separately |
| Resident needs and schedules | `project/native/vehicle_physics/{ResidentNeedsSystem,ResidentRoutineSystem,ResidentCommunitySystem,ResidentLifeSystem}.h` | Citizen errands, families, school escorts, venues and indoor visits | Extend existing population with ECS state and time-sliced updates | Some headers include legacy `modules/rts`; don't copy into modern engine |
| Building interiors | `project/native/city_interiors/{Module.cpp,README.md}`, `project/native/vehicle_physics/{CityInteriorHost,CityInteriorPlan}.h` | Swept wall/furniture collision, seeded room plans, on-demand GPU room meshes | Generic ECS room collision/navmesh residency, authorable multi-floor interiors | Project-specific floor plans; renderer/client may be separate patch |
| Enter / exit and boarding | `project/native/vehicle_physics/{VehicleEntrySystem,VehicleEntryAccess,TaxiPassengerHost,ServiceReturnSystem}.h` | Door-side access, slow/stopped constraints, boarding phases, return-to-service | Generalize one seat/door/action handshake in `engine/vehicles` | Requires matching new vehicle API and Motor/animation adapters; current headers use `RtsDriving` |
| Taxi | `project/native/taxi/{Module.cpp,README.md,tests.cpp}`, `project/native/vehicle_physics/{TaxiSystem,TaxiParking}.h` | Command 32810, fares, repair, loans, dynamic passenger pickup/exit, stale-quote validation | Generic transport-job + booking/receipt policies, example in game only | `kingston_taxi` appears in nativeModules array; concrete fares/destinations remain game data |
| Workshops | `project/native/workshops/{Module.cpp,README.md,tests.cpp}` | Repair, paint, validated quotes/receipts, saved ledgers | Reuse generic service transaction/paint/repair policy | `kingston_workshops` declared; don't copy 105 model definitions |
| Vehicle damage and suspension | `project/native/vehicle_physics/{CarDamageBox3D,VehiclePhysicsHost,VehicleSuspension}.h` | Hinged/detached panels, damage VFX, per-model spring/damper response | Generic optional presentation body/suspension bridge; separately implement authoritative wheel contact policy | **Presentation only** for Box3D panel damage and sprung pose, not saved or replicated; gameplay state remains independent |
| Car lights, audio, skid trails | `project/native/vehicle_physics/{VehicleSignals,CityAudioHost,VehicleSkidMarks,VehicleRoadLights}.h` | Lamps, blinkers, sirens, positional sound, tire trails | Existing engine sound/render/VFX owners; extract missing data-driven profiles | Some files already forward to shared AIVERSE policies |
| Emergency dispatch | `project/native/dispatch/{README.md,prepare_dispatch.py,dispatch_tests.cpp}` | Reuse a returning service vehicle and avoid duplicate call reservations | Improve shared incident dispatcher and tests | Python patches **isolated snapshot**, not a standalone clean engine API |
| Water/season/weather | `project/native/city_weather/{CityWeatherHost.h,README.md}` | Day/night, rain/wind, road wetting and puddles | Integrate via existing PHYSIS/NEREID/PRISMA rather than a third weather owner | Rain is presentation-only and does not change tire grip or pedestrian routing |
| Public transit | `project/scripts/{public_transport,rail_passengers,metro}.lua`, `project/native/vehicle_physics/{PublicTransportHost,RailPassengerHost}.h` | Bus/rail passengers, cabin/seat presentation, lines/route scripts | Reuse general transit schedules and seats, migrate remaining gameplay Lua | Rail sound != full rail runtime; some gameplay remains Lua |
| Towing, fire, municipal services | `project/native/{vehicle_wrecks,vehicle_fire,building_fire,port_transport,port_simulation,post,sanitation}/` | Native C++ event/cargo/incident systems, test fixtures | Generic fire/damage/dispatch/state policies only | Concrete city depots/transport/industry loop belong to project |
| Player home | `project/scripts/player_home.lua`, `project/native/vehicle_physics/PlayerHomeHost.h` | Home/flat, parking, camera, interior visuals | Generic ownership, room and property hooks; home as authored game content | Some logic remains legacy Lua |
| Source migration | `project/native/SIMULATION_MIGRATION.txt`, `project/AGENTS.md` | Explicit rule: C++ native game simulation, Lua view-only; list of migration debt | Preserve rule, move generic reusable policy to engine, not entire game features | Migration ledger not complete; old references may not compile against current main |

### Verified declarations vs mere source presence

`project/rules/native-modules.json` currently declares (14): `kingston_sanitation`, `kingston_post`, `kingston_utility`, `kingston_water`, `kingston_taxi`, `kingston_workshops`, `kingston_vehicle_fire`, `kingston_geometry`, `kingston_port_coordinates`, `kingston_port_transport`, `kingston_vehicle_wrecks`, `kingston_building_fire`, `kingston_port_simulation`, `kingston_interiors`. In contrast, the inspected files expose a `citylife` module and patched police behavior, but they were not separately declared there. Do not equate a directory, CMake subdir or README test history with current playable activation.

The external game's `project/game.json` still selects `"module": "rts"`. The original `project/native/vehicle_physics/CMakeLists.txt` expects a pinned engine snapshot, patches copied engine/client sources, and names old `modules/rts` headers. AIVERSE main has migrated ownership to `engine/game` and `engine/vehicles`. No direct cherry-pick or patch application is warranted without a compatibility bridge / generic extraction.

## Documentation and APIs changed

- Added `docs/open-world-framework.md` with OW-01 through OW-16, priorities, existing capability owners, acceptance gates, and first W1 target.
- Added this stage handoff.
- **No new public API. No source, game content, save schema, test, or ABI changes.**

## Tests and verification **actually run**

- GitHub: fetched repository metadata, directory trees, native policy and docs, current `main` commit refs, main rules script list and native module declarations.
- Source inspection only. **No local build, no CTest, no test runner, no gameplay capture, no macOS/Metal benchmark, no KINGSTON mount.**
- No test pass count claimed.
- This is a planning/ownership audit, not a completed runtime feature. Do **not** mark W1 `core` or site roadmap as `done`.

## Follow-up: next agent handoff

1. Start from the latest `main` plus this branch's roadmap; check no other agent has claimed W1, and follow the WIP limit.
2. Read `skills/workflow/aiverse-shared-capability/SKILL.md`, `docs/large-worlds.md`, `docs/no-genre-split-audit.md`, the world/scene ECS resource tests.
3. Before implementation, add a `world` system roadmap item `in-progress` in `docs/devlog/systems.json` (first stage commit) and record ownership.
4. Implement a small, testable ECS dynamic region-lifecycle slice (OW-01) **without external project code**. Distinguish deterministic authority from view residency.
5. Test with a synthetic two-region/one-car/one-passenger/one-destruction fixture. Require fresh save/load parity and no double entity after repeated crossings.
6. On Mac, build and test only with `/Volumes/KINGSTON/AIVERSE-builds/<branch>`, then run `./run-tests.sh --quick` on the merged tree. If KINGSTON is absent, stop the build.
7. Record exact branch SHA, changed files, actual test results, unresolved items, and next step in a new stage handoff **before** beginning any other feature.
8. Only after generic acceptance, evaluate optional external-game compatibility with a matching separate working copy; do not migrate game data into AIVERSE.

Release notes: planning only, nothing for 3.1.0 release yet.
