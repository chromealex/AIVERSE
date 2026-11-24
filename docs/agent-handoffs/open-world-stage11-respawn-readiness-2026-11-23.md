# Open World Framework — W1 stage 11: pre-admit engine-owned respawn destinations
Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Capability: W1 / OW-01, **partial**. Do not merge or mark as core.
Previous handoff: `docs/agent-handoffs/open-world-stage10-typed-vehicle-admission-2026-11-23.md`

## Problem established from source inspection

W1.9/10 added an authoritative staged movement API, but several
pre-existing gameplay owners still perform *implicit* teleports, inside the
already admitted tick. In particular:

- `ActorHost::tick` deathmatch respawns characters at `spawns_`.
- `ActorHost::tickSystems` bomb/round reset respawns characters at
  `spawns_` and calls `resetLootAndVehicles` to relocate cars to their
  initial map positions `initialVehicles_`.
- The shared `VehicleRace::after` calls `stepRaceSession`, then applies
  `respawnVehicle` to the last checkpoint/grid spot for wrecked, stuck or
  wrong-way typed racers. The race could cross another gate during the same
  tick, changing which checkpoint becomes the respawn position.

Existing `ActorHost::readyToStep` already pins `spawns_`. W1.11 plugs the
other two known gaps in the **existing** `CollisionStream` admission rather
than inventing a parallel loader or replacing the shared race physics.

## Code changes

1. `engine/vehicles/VehicleRace.h/.cpp`
   - New `VehicleRace::possibleRespawnPositions(const World&) const`
     returns sorted/unique candidates for active racers.
   - Uses real `lm::RaceState` + `lm::raceRespawnPoint`; includes the
     current grid/last checkpoint respawn and the next gate checkpoint
     that could become the current respawn during this same tick.
   - Pure read-only host query: does not move any car, consume RNG, alter
     RaceState, or depend on GPU/asset completion ordering. Uses shared
     `lm::valid` guards; does not include finished racers.
2. `engine/actor/ActorStreaming.cpp`
   - Under the existing opt-in `regionOwnership` condition, before
     admitting any simulation step, adds a 4-unit collision interest
     radius around both the possible race respawn positions and every
     `initialVehicles_` position. Existing player `spawns_` stays
     pinned; the same `CollisionStream::prepareRegions` and derived
     bot navigation readiness gate remain the only host entry barrier.
   - If these distant assets are not published, the step is refused by
     `GameCore::readyToStep` before commands/RNG/tick mutation.
   - Unopted games continue to use the old collision interest policy.
3. `tests/world/race_region_respawn_gate_test.cpp`
   - New pure-policy C++ fixture using actual `VehicleRace`,
     `lm::makeRace`, `World` / `vehicles::RaceState`, with two real
     checkpoint definitions and one grid racer.
   - Checks initial grid + possible first gate, previous/next checkpoint,
     terminal checkpoint, finished racer exclusion, and read-only repeat
     calls (race-state hash unaffected).
4. Root `CMakeLists.txt`
   - New target `aiverse_race_region_respawn_gate_test`, links
     `aiverse_vehicles`; CTest name `race_region_respawn_gate_test`,
     labels `World;Vehicles`.
5. `tests/world/actor_region_admission_test.cpp`
   - Added `testOriginalVehicleResetDestinationPreloaded`: uses actual
     GameCore/ActorHost with `mechanics.enabled`, a vehicle whose
     initial position is the remote collision chunk, then relocates
     the live car near the players. Refuses the next tick while the
     remote original reset destination is unavailable, keeps RNG and
     checksum, and allows progression after that chunk recovers.
6. `docs/region-entity-ledger.md`
   - Stage11 behavior, API, scope, test status, and fail-closed
     residency-budget limitations.
7. This independent agent handoff.

## Commits

- `49bcb7184de5b40a175d5aa8b1419a943d42777b` — shared race query API.
- `e5b18499984c18e7bd8ede5b735c2db0a8b107eb` — pure current/next race
  respawn positions.
- `aef0f3a401684da4c92870416c38637c93e4b49f` — ActorHost race interest.
- `c19f98315aa7a9de1346cace6ba57b07fb94d018` — pure race CTest fixture.
- `214e6816ebf47a6af8add1d4372b058d1a8d8f55` — CMake test target.
- `616dd562b0ce1b16808c93ee2c156646ef0d97cf` — valid final race fixture.
- `648da0ccaf1de863ff15c309465e6ec77713bca6` — original car spawn pin.
- `dbaf2b5983b7378a8a9d4040267caf524738cdb3` — GameCore test for remote
  initial car spawn.
- `7e6b0063bfda16d414f13d3bfa43b27b54db1835` — Stage11 API docs.

## Tests — authored versus actually executed

**Authored**: The new `race_region_respawn_gate_test` and an extension of
`actor_region_admission_test` verify the described invariants in source.

**Actually executed**: NO new native CMake build, CTest, merged-tree Mac
quick suite or real device/game capture. This tool runtime cannot access
the owner's required Mac and writable `/Volumes/KINGSTON`. Source
inspection and successful GitHub commits are NOT execution evidence.
Do not claim the tests PASS or the streaming system is production-ready.

## Limitations / risks

- Race target admission is deliberately conservative: a live racer can
  require a distant grid/previous checkpoint **and** next gate to be
  collision-resident simultaneously. In a large track with an undersized
  `maxResidentChunks`/bytes budget, this fails closed and may cause a
  non-progressing match. A future policy should inspect the exact
  deterministic trigger and demand only guaranteed possible targets,
  coupled with explicit capacity-aware host readiness.
- The race query computes the most likely immediate respawn targets.
  Multi-gate crossings within a single tick, out-of-order corrupt
  race states and external teleport writers are outside the guaranteed
  supported movement set. Need real high-speed race replay tests.
- Only world-space maps opted into `rules.regionOwnership={}` use this
  new gate. Unopted gameplay behavior and save format remain unchanged.
- Other direct coordinate writes in gameplay/scripts/royale path are not
  universally routed through staging. Not an all-engine transactional
  motion protocol.
- W1 region ownership currently scans live actors and all ledger records
  every tick. Cold actor/car gameplay components, world disk paging,
  crash-recovery journals, scalable streaming NavMesh and deterministic
  cold-region time are not complete.
- Branch previously 40 commits behind main; re-check ancestry and
  integrate without overwriting concurrent agent work before Mac
  acceptance.

## Next agent steps — W1.12 and acceptance

1. Test first: on the actual Mac/KINGSTON environment, configure and
   build W1 CTests including
   `aiverse_race_region_respawn_gate_test` and
   `aiverse_actor_region_admission_test`, run `ctest --output-on-failure`,
   then `./run-tests.sh --quick` on a merged integration tree. Record
   real SHA/compile failures and pass counts.
2. Test full race course session with remote-grid/checkpoint collision
   failure and recovery: verify no partial race state, command/RNG/tick
   changes on unavailable targets and resumed deterministic replay.
3. Implement capacity-aware deterministic race respawn admission and
   explicit destination demand for non-race script/native/royale teleport
   owners without adding a genre-specific simulator.
4. Replace full-scan ownership reconciliation with event-based region
   indexes; add bounded durable region journal and crash recovery.
5. Introduce dormancy-safe AI/vehicle gameplay ownership and disk
   checkpoint-based eviction only after owners explicitly exclude
   dormant components; add p95/memory benchmarks and visual captures.
6. Always create the next stage's separate handoff and keep PR Draft
   until the merged Mac quick-suite and integration acceptance pass.

W1 remains **partial**, not a merge/release candidate.
