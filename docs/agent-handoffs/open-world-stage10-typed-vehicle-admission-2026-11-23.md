# Open World Framework — W1 stage 10: pre-admitted typed vehicle teleports

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Pull request: https://github.com/chromealex/AIVERSE/pull/116 (**Draft**)
System: world / W1 / OW-01, **partial**, unmerged.
Previous handoff: `docs/agent-handoffs/open-world-stage09-pre-tick-admission-2026-11-23.md`

## Technical context checked

Real world-space `GameCore` delegates to `actor::ActorHost`. W1.9
already provides a persistent ECS `RegionMoveRequests` queue, a
`readyToStep` collision/NavMesh admission gate and an early
`actor.regionMoves` system. That stage deliberately refused teleports
of an entity with `vehicles::VehicleDrive`.

This stage inspected and reused the existing owner paths:

- `engine/mechanics/VehicleControllers.h` defines
  `lib::mechanics::VehicleState`, `respawnVehicle` and
  `vehicleWorldPoint`.
- `engine/vehicles/Vehicles.h/.cpp` owns the single
  `vehicles::VehicleDrive` and `VehicleOccupant` ECS components.
  `vehicles::stepVehicles` remains the sole ordinary physics tick.
- `engine/actor/ActorVehicles.cpp` owns world-space typed
  `VehicleType` definitions, `ActorHost::VehicleWorld::placed`,
  driver/seat player-slot resolution and the authoritative
  `actor::CharacterBody` positions.
- `engine/mechanics/VehicleData.h` parses real `vehicleTypes`
  definitions and typed `driver` / `passenger` seat roles.

## Implemented code

1. `engine/actor/ActorModule.h`
   - Public, read-only `ActorHost::typedRegionMoveGroup(const Simulation&,Entity,
     vector<Entity>&,string*) const`.
   - Private `placeTypedRegionMove(Simulation&,Entity,Vec3,string*)`
     commit for a *previously pre-admitted* typed vehicle target.
   - Read-only group helper is public because the common movement
     policy builds prospective groups outside ActorHost's member functions.
2. `engine/actor/ActorVehicles.cpp`
   - Added `typedRegionMoveGroup`: validates matching
     `Vehicle`+`VehicleDrive`, valid shared vehicle controller,
     registered type, driver player-slot ↔ Entity correspondence,
     every assigned seat's definition, CharacterBody.vehicle links,
     no omitted or reverse-only occupants, and exact complete
     group membership via the Stage06 shared
     `VehicleRegionTransfer::gather`.
   - Added `placeTypedRegionMove`: validates again, creates the
     new controller via the existing `lib::mechanics::respawnVehicle`
     at the admitted position (preserves yaw and permitted gear/time
     state, resets unsafe motion), updates the authoritative
     `Vehicle.pos/yaw/velocity`, sets every seated character's
     canonical `CharacterBody.pos/velocity` using the actual seat
     offset through `vehicleWorldPoint`, and synchronizes existing
     `VehicleOccupant` reverse poses.
   - No new vehicle physics or seat component; normal
     `vehicles::stepVehicles` still owns subsequent movement.
3. `engine/actor/ActorRegionAdmission.cpp`
   - Previously rejected typed cars in the common `moveGroup`
     helper; now delegates to `ActorHost::typedRegionMoveGroup`.
   - The preflight `Commit` remembers the admitted destination and
     whether the target has `VehicleDrive`. An admitted typed car
     commits through `placeTypedRegionMove` instead of translating
     each seat by a loose delta. Untyped arcade car and on-foot paths
     remain as in W1.9.
   - Preserves W1.9's exact pre-admitted-only request cache, late
     command refusal, Native ECS request persistence and collision
     streaming admission before the tick.
4. `tests/world/actor_region_admission_test.cpp` (extended existing CTest)
   - Real `GameCore` / `ActorHost` typed wheeled vehicle definition,
     two actual seats and player slots, shared `VehicleDrive` and
     `VehicleOccupant` ECS links.
   - Deliberately failing hashed remote collision read preserves
     authoritative controller pose, RNG, checksum, commands and tick;
     eventual retry admits the teleport and two peers match.
   - Asserts controller and Vehicle canonical positions, seated
     CharacterBody poses, Reverse VehicleOccupant pose, car+all
     passengers' W1 region owners, seat continuity, and Native ECS
     v10 save/reload and COW rollback.
   - Invalid passenger assignment and extra reverse-only occupant
     fail *before* queue mutation.
   - Does not change the existing CTest target name:
     `actor_region_admission_test`, built under
     `aiverse_actor_region_admission_test` with
     `aiverse_authority`.
5. `docs/region-entity-ledger.md`: Stage10 public behavior, tests,
   limitations and ownership boundaries.
6. This handoff document.

## Commits

- `327ea5896165573b094b0d7b650460dbfbdd2c38`
  public/private typed-region ActorHost method declarations.
- `8be68e5d32f155cd72292568618a6cf0b39b2be7`
  typed controller respawn, seat positions and reverse-link sync.
- `1a53b7feefbcc9377112b77e308813e5c05ba2d9`
  typed group admission and specialized controller commit.
- `0014bd91967c5c80cef35578b277a24050269410`
  expose the read-only helper to the shared movement preflight.
- `04507745cb3238ecf375d01d1836ec7d922d087d`
  real typed vehicle two-seat admission/retry/save CTest source.
- `8b9e297c39f9daf8b7e9a1406e6836de5ce77479`
  reject hidden/extra reverse-linked occupants or driver absent from
  actual seat map.
- `2992b73564de4054ac1af4cb5adc2710cd8209b4`
  avoid stale ECS component pointer after a COW restore.
- `5c06420a958c495922d9d797b3908360fd47bfe7`
  invalid seat/backlink refusal CTest source.
- `37ef05ecc1c85ebf27d7f96eedf4840f7d29cba7`
  Stage10 public API documentation.

## What is not delivered

- **Not a verified, production-safe typed teleport yet.** Native
  C++/CTest targets have NOT been built or run on the required
  writable Mac/KINGSTON environment. Every test claim above refers to
  authored assertions, not actual PASS output. PR remains Draft.
- `respawnVehicle` deliberately resets motion. W1.10 does not
  preserve arbitrary high-speed physics while crossing streaming
  boundaries; normal `vehicles::stepVehicles` continues to drive.
- W1.9 only intercepts callers that explicitly request staged moves.
  Direct writes to `CharacterBody.pos`, `Vehicle.pos`, race
  respawns and some gameplay script/native movement are not yet
  wrapped. W1.8 may still throw *after* those systems mutate a tick
  if they teleport into unloaded collision.
- `VehicleOccupant` reverse components are checked/synchronized at
  runtime; ActorHost's own older disk serializer primarily stores
  the controller/seat manifest and character snapshots. A separate
  acceptance task must confirm reverse component derivation after
  load where another owner depends on them. Do not claim disk
  persistence of all reverse components from this test alone.
- The multi-component live ECS commit is prevalidated for logical
  errors, but allocation exceptions are not guaranteed atomic
  across Component pools; a proper transactional Entity-level ECS
  operation is still needed.
- No general outdoor navmesh or disk-backed region snapshot,
  crash recovery, offscreen NPC simulation, streaming ECS component
  ownership/eviction or p95 scale benchmarks.
- This branch was around 40 commits behind main during prior W1
  work. Recheck at acceptance: do not blindly overwrite concurrent
  changes.

## Tests/evidence actually performed

- Inspected the exact current GitHub source contracts for
  `VehicleDrive`, `VehicleOccupant`, `VehicleState`,
  `respawnVehicle`, `vehicleWorldPoint`, `ActorHost`,
  `VehicleRegionTransfer`, native ECS persistence and staged
  region request code.
- Authored and committed the specified code/test/docs to GitHub,
  and inspected the source.
- **No CMake build, native CTest, full `./run-tests.sh --quick`,
  merged-tree platform parity, GPU capture or performance test
  executed.** The owner's macOS `/Volumes/KINGSTON` volume is not
  available in this runtime. Leave W1 `partial`.

## Next agent handoff — W1.11

1. **Test before new functionality.** On the user's Mac, fetch
   current `main`, reconcile concurrent work into a dedicated
   integration worktree with build output exclusively on
   `/Volumes/KINGSTON/AIVERSE-builds/...`. Compile and run all
   W1 test targets, particularly `actor_region_admission_test`,
   `actor_region_lifecycle_test`, and existing `world_async`.
   Fix actual build/test failures, record true SHA, pass/fail logs.
2. Test typed car with a real seat manifest and direct save/reload;
   confirm `VehicleOccupant` derived reverse links restored
   correctly and seat-controller/nav physics remain consistent.
3. Refactor all remaining authoritative teleport/respawn pathways
   (round resets, race respawn, scripts, native modules) through a
   deterministic preflight/admission boundary, before a normal
   fixed-tick gameplay mutation. Test denied collision/NavMesh
   chunk with unchanged tick/RNG/commands.
4. Refactor full-scan ledger hot/cold state to deterministic
   indexed lifecycle events; implement bounded region journal.
5. Add actual safe NPC/vehicle dormant transitions, disk snapshots,
   background cold clocks and crash recovery; benchmark world scale.
6. Maintain an independent handoff and screenshots only when
   actual gameplay acceptance was run. Merge PR into main only
   after green Mac quick-suite and no unresolved conflicts.

No release claim; feature remains W1 partial.
