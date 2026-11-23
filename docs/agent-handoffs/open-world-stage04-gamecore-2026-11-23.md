# Open World Framework — W1 stage 04: real GameCore capability integration
Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Baseline main: `aec89ef435aba457ebeec24ef196f62a2fbb611d`
Area: W1 / OW-01, **partial**. Do NOT mark the world roadmap complete.
Previous handoff: `docs/agent-handoffs/open-world-stage03-cold-ecs-2026-11-23.md`

## Changes in this stage

A concrete path from a **real GameCore native C++ module** to the generic world region ledger/park/wake controller has been added, without introducing a genre-named host and without modifying old games which don't opt in.

1. `engine/world/RegionLifecycleRuntime.h` — shared `RegionActivationQueue` ECS persistent resource, validated and bounded Archive codec, explicit `RegionLifecycleRuntime::request`, deterministic `step`, setup-time `install` registering existing W1 resources/components and an active Simulation system.
2. `tests/world/region_gamecore_integration_test.cpp` — real `game::GameCore` + `game::NativeModule` fixture, attaching stable identity to an existing live map `game::Unit` and parking a test-only `RegionWork` component via command 61537.
3. `CMakeLists.txt` — real GameCore test target `aiverse_region_gamecore_integration_test`, CTest `region_gamecore_integration_test`, linked to `aiverse_game` and labels `World;ECS`.
4. `docs/region-entity-ledger.md` — stage 04 lifecycle and native integration API, limitations and test scope.
5. This stage handoff.

Code commits:
- `b801ac56be1060025f55407775223afbc164e138` — gameplay-owned scheduler and persisted request queue.
- `80ae5aa6bd9f0939efe5ec8dc029010625718b51` — GameCore real-host native integration source.
- `3bd97f52093164cba85e8fd98b6932f19eab177c` — CMake test.
- `912a63faf9caf2af2027365e494d03390e9114d0` — missing ECS binding/fail-closed pending-request case.
- `e664d39875a7610ad5eaa1a5c7e408c584cc595d` — documentation.

## API/architecture

`RegionActivationQueue` (persistent resource stable ID `aiverse.world.region_activation_queue`, schema v1):
- sorted `map<Cell,uint8_t>` pending targets, 0=cold, 1=hot; `applied` monotonically counts successfully handled requests.
- maximum 1024 pending regions, bounded explicit portable Archive serializer, transactional read with duplicate-coordinate and invalid-flag refusal.
- no implicit camera/visibility dependencies and no background thread calls.

`RegionLifecycleRuntime::install(Simulation&,RegionDormancyController,uint32_t maxTransitionsPerTick)`:
- only invoked by a game's authoritative C++ setup (the test uses NativeModule::setup via real GameCore).
- opt-in persistent registration for ledger, stable identity, cold store, dormant tag and activation queue.
- sets empty resources and appends an immutable-codec `world.regionLifecycle` simulation system.
- limits the work budget to 1..16 region transitions per fixed tick; duplicate installation rejected.
- new schemas are present only for games that opt in. Regular GameCore setup without the module is unchanged.

`RegionLifecycleRuntime::request(Simulation&,Cell,bool)`:
- must be called only after command/player/action validation or deterministic server/gameplay policy.
- coalesces the target of each region to the latest requested 0/1 status.
- remains authoritative ECS state and survives saved games/rollback.
- does not itself move any Entity or change collision/navigation.

`RegionLifecycleRuntime::step(Simulation&,const RegionDormancyController&,uint32_t budget)`:
- processes sorted region keys, each via `RegionDormancyController::setRegionActive`.
- on success removes pending request, increments `applied`.
- on validation failure retains the pending head and does not process later regions that tick.
- intentionally **not** an admission gate: W1.5 must use `GameModule::readyToStep` with collision/NavMesh before allowing actors to enter unready regions.

Existing `RegionDormancyController` keeps Entity+Transform+stable identity while parking selected registered component payloads. The host must mark only components safe to pause; in particular do NOT automatically park `game::Unit`, `Mobile`, `vehicles::VehicleDrive` or `VehicleOccupant` without updating their authoritative owners.

## Tests authored vs actually verified

Test source `tests/world/region_gamecore_integration_test.cpp` includes:
- Initializes two genuinely independent `GameCore` simulations with actual map-spawned `game::Unit` entities. A C++ `NativeModule` enables the shared W1 lifecycle using the existing `rules.nativeModules` command wiring.
- Valid player request deactivates a region's registered `RegionWork` component, leaves GameCore Unit, Transform and Entity alive; reactivates the component from cold data.
- Native module tick skips `RegionDormant`; does not receive or apply an unvalidated command from a different player.
- Real Simulation serialized save → fresh GameCore restore, same checksum, same assignment; hot state resumes tick processing.
- COW rollback restores live component after a subsequent cold request.
- Missing materialized ledger ID blocks a queued request without touching existing actor, remains queued; hot as well as cold requests fail conservatively until ownership is repaired.
- A normal GameCore run **without** this native module has no W1 ECS resources at all.

**Source verification actually performed:** fetched real `GameCore::setup`, `NativeModules::load/command/tick`, `Simulation::setup/step/serialize` interfaces, connected current W1 sources, CMake pattern from existing `ecs_native_rts_integration_test`, and the current W1 branch ancestry. Created the new header/test/CMake/docs and verified GitHub accepted those commits.

**NOT executed:** C++ compile, CTest or the Mac/KINGSTON quick suite; no native runtime execution, no screen capture, no p95 benchmarks. This environment cannot build in required `/Volumes/KINGSTON`, and user policy forbids alternate build trees. Test descriptions here are what test code *asserts*, not a passed-results report. Do not claim a PASS.

## Explicit limitations

- This stage is **real GameCore integration via native module**, not automatic host-wide region management. The production owning game must opt in, register safe component codecs, tag relevant actors, and submit validated activation requests.
- Demonstration parks a module-owned, test-only `RegionWork` component on an actual GameCore `Unit`. It does not park the Unit, its movement, physics, AI or transport controller. A full NPC/car lifecycle requires coordinating those component owners.
- No automatic region interest algorithm, collision/NavMesh readiness barrier or dynamic world streaming from disk.
- A queued blocked transition remains pending; if authoritative data is never repaired, progress is blocked. The caller can replace the target request.
- The scheduler does not make structural ECS pool changes exception-atomic; allocation failures during hot/cold commit remain a host boundary.
- The authority queue has bounded region-entry count, but the initial cold-store implementation copies its map for preflight and still keeps all Entity/Transform objects resident; not a proof of AAA memory budgets.
- The test uses external template content like existing engine integration tests; no user project/game source is copied into the engine.

## Next agent steps (W1.5)

1. On Mac with writable KINGSTON volume, compile the four W1 test targets on latest merged tree:
   `aiverse_region_entity_ledger_test`, `aiverse_region_ecs_bridge_test`, `aiverse_region_cold_store_test`, `aiverse_region_gamecore_integration_test`.
2. Run `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug -R '^(region_entity_ledger_test|region_ecs_bridge_test|region_cold_store_test|region_gamecore_integration_test)$' --output-on-failure` and then `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug`. Diagnose and fix actual compiler/test failures before touching another subsystem.
3. Implement collision/NavMesh region readiness barrier via the `GameModule::readyToStep` host hook, ensuring failed destination loads do not advance tick, commands or RNG. Keep deterministic authority decisions separate from the wall-clock duration of GPU/asset streaming. Test teleport and two observers with failed loads.
4. Integrate actual `engine/vehicles` occupant lifecycle **only** after its owning systems explicitly skip dormant components and atomic cross-region groups are enforced.
5. Add disk-backed per-region durable snapshot/checkpoint and derived navigation invalidation with restart/replay tests. Store large-world dynamic payloads without retaining all ECS entities indefinitely; define stable ID-to-Entity remapping and outside references.
6. Record real performance, Mac platform/build results, and prepare visual devlog/demo only after verified screenshots. Maintain per-stage handoffs.

W1 status stays partial, site world roadmap in-progress, PR #116 Draft, and no 3.1.0 release claim.
