# AIVERSE Open World — W1 stage 12: budget-aware race collision admission

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Working branch: `feature/aiv20261123_open_world_audit`
PR: https://github.com/chromealex/AIVERSE/pull/116 (Draft)
W1 / OW-01: **partial**, not production accepted
Previous handoff: `docs/agent-handoffs/open-world-stage11-respawn-readiness-2026-11-23.md`

## Goal and source inspection

W1.11 added pre-tick collision residency for potentially distant race respawns and original vehicle reset locations. This could keep unrelated distant collision chunks pinned for every tick, even when a gate/round reset is not possible. On maps with strict resident chunks/bytes, an impossible simultaneous pin set could block indefinitely without an explanatory diagnostic.

Inspected and reused:
- `engine/world/CollisionStream.*` and `WorldAssets::intersecting`: stable, deduplicated manifest collision cells.
- `StreamLimits`: residentChunks and residentBytes; `AsyncChunkStream::Status` is host-local async resource state, not gameplay authority.
- `engine/vehicles/VehicleRace`, `lm::raceRespawnPoint`, `lm::RaceState`, `VehicleDrive::controller` and `lm::kVehicleMaxSpeed`.
- `ActorHost::readyToStep` admission before `Simulation::step` consumes commands/RNG/tick.
- `ActorHost::tickSystems` resets cars only when bomb/round match reset is due (`config_.mode=="bomb"` and `match.end <= tick`).

## Code changes

1. `engine/vehicles/VehicleRace.h/.cpp`:
   New read-only `imminentRespawnPositions(const World&,Fixed maxTravel)`.
   Keep `possibleRespawnPositions` unchanged for existing hosts.
   - Always pin the current last-checkpoint/grid respawn of each active racer.
   - Pin the next-gate respawn only when the registered `VehicleDrive` controller can approach the gate aperture within `maxTravel` (`distance <= maxTravel + gate.radius`).
   - A missing/invalid controller, stale controller/race sample or disabled pruning falls back to the conservative previous candidate set.
   - Sort/dedupe. Never mutate race state, vehicle controller, RNG or ECS.
2. `engine/actor/ActorStreaming.cpp`:
   - In opted-in W1 world-space maps, pass `lm::kVehicleMaxSpeed * sim.dt() + 4_fx` as a conservative fixed-tick movement upper bound. Large distant *next* checkpoints no longer remain pinned just because they are in the course.
   - Original `initialVehicles_` collision only pinned on the actual due bomb/round reset tick, not always. Normal `spawns_`, current vehicles, projectiles, explicit `RegionMoveRequests` continue to be handled by the same existing interest logic.
   - Call `CollisionStream::canFitRegions` before starting async preparation. On guaranteed budget overflow refuse the tick and expose an actionable host-local error.
3. `engine/world/CollisionStream.h/.cpp`:
   New pure `canFitRegions(std::span<const Bounds>,std::string* error)`.
   Uses actual `WorldAssets::intersecting` and sums the manifest's reserved bytes, safely comparing to `residentChunks/residentBytes`. Reports `collision pin set exceeds maxResidentChunks` or `...maxResidentBytes`. Does not schedule loads, evict valid physics, mutate any simulation state or consume RNG.
4. `engine/actor/ActorModule.h`:
   New diagnostic `collisionAdmissionError()` and a host-local error field reset each new ready-check/reader. Not part of native ECS persistence or game checksum.
5. `tests/world/race_region_respawn_gate_test.cpp`:
   Extended old source fixture to cover far gate pruning, close gate retention, stale/no controller fallback and backwards-compatible full candidate query.
6. `tests/world/actor_region_admission_test.cpp`:
   Added `testImpossibleCollisionBudgetReportsError` (two distinct collision cells but `maxResidentChunks=1`), fail-before-tick with unchanged checksum and retained request, then replacement with a nearby destination can progress.
   Updated the original-vehicle reset fixture to use a real bomb match with a required bomb site and to assert the remote starting vehicle position is not pinned in ordinary play but is pinned when `match.end` makes reset due.
7. `docs/region-entity-ledger.md`: added stage12 public API, actual limits and verification status.
8. This independent agent handoff.

## Actual commit trail

- `8c73829df2e60863ff34b31cb10f02b920ad2218` — bounded shared-race query API.
- `2f42e69879b6838c40cb6477f48f29a16daefd0a` — initial bounded candidate query.
- `b775c8ab9bb7e9c1a93877b2331d7d9b8ca1dcbb` — correct 3D aperture bound (a plane-distance-only check was insufficient).
- `356529787f72d02d56a65ef418671c1b11d29e46` — ActorHost calls bounded query.
- `98d0475200891240938ed8267b42d3a1d31ecf9d` — bounded query source tests.
- `15a5dd3f1bb329e457f7ea71250ad8b09e7cca6f` — collision capacity API.
- `2284d5ce53b67475c84a61049317117eb0db192f` — capacity preflight.
- `4935dfd6bb8768746d2be0762830fb9e1d67eae6` — ActorHost error accessor.
- `8d2ca00fbba734c7fbc18d6745af03aa6d5a84d6` — expose impossible budget before async update.
- `0efe1477327bf6f1f8293649161af20b5efc3324` — capacity fail-and-retry CTest source.
- `d6c58858fa989c869e7793a882129b3be33976a6` — due-round-only initial vehicle pins.
- `64b690382f1438256aa81c596fe1638c95b1aa1a` — due bomb reset fixture.
- `ad3c107b0870f7a82653938f3d5a8b83a1bc96a3` — stage12 API docs.

## Tests: source versus execution

**Authored**:
- Pure shared race candidate test exercises current respawn, unreachable next, reachable next, stale/missing controller fallback and no state mutation.
- World-space real GameCore stream admission test exercises impossible simultaneous chunk budget and existing request retention, as well as due-only bomb round reset.

**Actually executed**: Source inspections via GitHub and commits. NO CMake build, NO CTest PASS, NO `./run-tests.sh --quick` and no real Mac/KINGSTON integration or GPU gameplay capture. This environment cannot access the owner's writable `/Volumes/KINGSTON/AIVERSE-builds` volume. Do not claim test cases passed.

## Known limits

1. The maxTravel pruning is safe for ordinary valid, bounded fixed-tick controller motion; if another game system directly teleports a racer by writing its controller pose, it bypasses the assumption. Such writers must use explicit W1.9+ staged admission.
2. Current respawn target is always mandatory. If many racers' mandatory current targets exceed the resident chunk/byte budget simultaneously, the match still blocks (now with a diagnostic). This is the correct failure mode until a higher-level deterministic capacity/interest strategy is designed. Never simulate collision holes.
3. The budget estimate sums manifest reservations. It does not account for every runtime arena/staging overhead and should not be treated as a guarantee that async publication cannot fail for another reason.
4. A permanent change of controller state between readyToStep and the actual tick via an unregistered path is still outside the engine-wide motion transaction policy.
5. W1 ActorHost region ownership still scans tracked pools and ledger tombstones each tick. Full ECS hot/cold owner participation, disk-backed region journal/eviction, cross-region AI and large-world performance remain.
6. This branch diverged from `main` during W1 work. Fetch current ancestry and reconcile conflicts on a test branch before merge; do not overwrite concurrent agents' work.

## Next agent W1.13 — test and general motion boundary

1. On the user's macOS machine with writable KINGSTON, reconcile this feature branch with current `main` in a separate integration worktree. Build and run all W1 test targets, especially `race_region_respawn_gate_test`, `actor_region_admission_test`, `actor_region_lifecycle_test`, and `world_async`. Capture actual compiler errors, test status, HEAD and performance.
2. Run the merged-tree `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug`. Correct API/test errors before further feature work.
3. Build a shared authoritative *motion admission contract* for script/native/gameplay teleport writers and race respawns; preserve the existing shared VehicleDrive controller and canonical ActorHost component positions, no dual physics.
4. Replace full-scan region ownership with event-driven indexed lifecycle and bounded disk journal, retaining deterministic save/replay semantics.
5. Implement actual dormancy-safe NPC and vehicle component ownership (AI and transport must skip dormant), disk snapshots, cold clocks and crash recovery; demonstrate with real gameplay and benchmark p95.
6. Publish another handoff with tests **actually run**. W1 remains Draft/partial until Mac acceptance.

No release claim, no merge to main.
