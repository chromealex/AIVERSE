# AIVERSE — Open World W1.15: explicit ECS region dirty observations

Date: 2026-11-23
Repo: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Area: W1 / OW-01; **partial**, not production-accepted
Previous handoff: `docs/agent-handoffs/open-world-stage14-revision-change-feed-2026-11-23.md`

## What was established from actual source inspection

The generic `engine/core/ECS.h` World has structural
`create/destroy/add/remove/flush` and native ECS/COW snapshots, but
does not expose guaranteed `onChanged` events for arbitrary writes
to `CharacterBody.pos`, `Vehicle.pos`, `Loot.pos` or typed
vehicle controllers. The real world-space `ActorHost` has several
existing direct mutable writers and stage14's revision change
feed only describes **already committed ledger changes**.

Therefore removing ActorHost's complete fallback traversal would
lose real movement/creation/deletion events. Stage15 introduces
a bounded, opt-in **event producer/consumer contract** from
participating real gameplay owners while keeping those scans.

## Actual code changes

1. **New `engine/world/RegionDirtyQueue.h`**
   - `RegionDirtyKind {Spawn=1,Move=2,Retire=4}`.
   - `RegionDirtyQueue {map<stablePackedEntityPlusOne,uint8_t> entries;
     uint64_t acknowledged}`, maximum 65,536 distinct pending IDs.
     Repeated same-tick notifications for one actor are OR-coalesced
     into one map entry; stable sorted key order is deterministic.
   - `mark`, `marked`, native portable Archive `io` schema v1,
     staged transactional read, bounded count, duplicate/bad bit
     rejection, and `registerRegionDirtyQueue` (native ECS
     persistent-resource registration).
   - Dirty queue is game-authoritative pending notification state
     stored with World/COW snapshots and ECS v10 serialization,
     not camera/renderer residency state.
2. **`engine/actor/ActorModule.h`**
   - Includes the shared dirty queue and exposes
     `ActorHost::reportRegionChange(sim,entity,kind)`.
   - No new genre host and no duplicate ECS/vehicle controllers.
3. **`engine/actor/ActorModule.cpp`**
   - Registers/initializes RegionDirtyQueue **only** when
     `rules.regionOwnership={}` opt-in exists. Unopted games
     preserve their legacy persistence schema and behavior.
   - Existing `spawnLoot`, `spawnVehicle` report Spawn.
   - Existing round-reset loot destruction reports Retire.
4. **`engine/actor/ActorSystems.cpp`**
   - Existing real gameplay loot-pickup destruction reports
     Retire before calling the normal `World::destroy`.
5. **`engine/actor/ActorRegionAdmission.cpp`**
   - Already admitted canonical actor/car/occupant movements now
     report Move for all affected members. Collision/NavMesh
     admission and the shared typed VehicleDrive seat/controller
     code remain the movement owners.
6. **`engine/actor/ActorRegionOwnership.cpp`**
   - Implements `reportRegionChange` with queue bounds checking;
     no-op when W1 is not active.
   - At the fixed-tick region sync boundary, reads queued dirty
     IDs in stable order. If destroyed/nontracked and an existing
     ledger owner is still live, tombstones it; if newly tracked,
     binds its canonical host position through the shared
     `RegionEcsBridge`.
   - Continues full CharacterBody/Vehicle/Loot scan with grouped
     car+passenger movement and older direct-write compatibility.
   - ACKs and clears the queued bits only after successful
     reconciliation. Failure leaves observations pending.
   - Reacquires ledger const resource after mutable writes to
     avoid retaining a stale COW snapshot pointer.
7. **`tests/world/region_dirty_queue_test.cpp`** (new)
   - Exercises OR-coalescing flags; deterministic codec; truncated
     and bad-schema decode preserving previous queue state;
     COW rollback and recycled Entity generations.
   - Registered in root `CMakeLists.txt` as executable
     `aiverse_region_dirty_queue_test`, CTest
     `region_dirty_queue_test`, labels `World;ECS`.
8. **`tests/world/actor_region_lifecycle_test.cpp`**
   - Existing actual ActorHost fixture now sends explicit loot
     spawn/retire marks, verifies queue ACK and clearing,
     verifies direct `Loot.pos` change still migrates through
     fallback with no Move mark, and checks native ECS load
     restores the acknowledged counter.
9. **`docs/region-entity-ledger.md`**
   - Stage15 public API, producer contract, scope, limit and
     compatibility guarantees.
10. **`skills/world/aiverse-region-lifecycle/SKILL.md`**
    - Explains dirty-event producer usage, bounded coalescing,
      necessity of fallback, tests and limitations.
11. This independent handoff.

## Commit trail (stage15)

- `bb5fe28bcb3fdd73ba15eb2e9934c3e241e489f7`
  — bounded persisted dirty queue.
- `28cba79057f7857817f061c805a4db2a89830715`
  — ActorHost reporting method declaration.
- `5174033d7606e0074f0fdf9d2f2621c26a3144f9`
  — opt-in registration and spawn/reset hooks.
- `bfe91c13955ec2c3b5c90d26d0a470083f178629`
  — consume dirty observations before fallback scan and ACK.
- `79e39aeb6f2246a2da111d03f751fc1ddf01cd37`
  — staged teleport movement producer.
- `34006015929ae5164bb886b5dd1d510ad3c1d84e`
  — pickup loot destruction producer.
- `dcb49260ed4b34643f5afe472cbaa55a2f258b03`
  — ActorHost spawn/retire/save/legacy-movement test.
- `38a48b8b670b5396bbb284dd091185051c8ba747`
  — dirty queue standalone test.
- `fdc8ecb6bcf4f3f9607baecd045fbe249a824224`
  — fix lifetime of truncated Archive input in test.
- `8aad9f638da753fb3cdac5b6fb7650202c655555`
  — register CTest.
- `13a947719c026b5af0825b639658d763e68e6330`
  — reacquire ledger after COW resource detaches.
- `a84ac0e337671a019e06a22df299f85ad7714ab1`
  — Stage15 public documentation.
- `884cf37ef6551cf16111debef2afecf2954eba59`
  — update skill for next AI agents.

## Tests actually performed vs merely written

**Performed:** inspected current GitHub API/source for World ECS,
ActorHost gameplay producers, existing region sync, COW interfaces,
and CTest registration patterns; created/updated the code and
verified GitHub commits. Source-level checks only.

**NOT performed:** native C++ compile, execution of
`region_dirty_queue_test`, `actor_region_lifecycle_test`,
any earlier W1 tests, merged Mac/KINGSTON quick suite, native
save/rollback crash test, game capture, or performance profiles.
The required user's macOS and
`/Volumes/KINGSTON/AIVERSE-builds` runtime is not accessible
in this session. Do not claim test PASS, complete W1, or merge.

## Known limitations / next agent work W1.16

1. **Run Mac tests first**: reconcile this branch with current main
   on a separate test worktree, build all W1 CTest targets under
   `/Volumes/KINGSTON/AIVERSE-builds/...`, and run
   `./run-tests.sh --quick`. Fix actual compiler/test failures
   and record exact SHA/results.
2. Dirty queue covers **only participating** owners: ActorHost
   loot/car creation, loot destruction, and already admitted
   teleports. Character movement, regular typed-vehicle physics,
   externally created ECS actors and arbitrary direct field
   assignments are not fully instrumented. Full actor-pool
   fallback is still mandatory and still O(active entities).
3. The dirty queue coalesces bits and ACKs after sync; it is
   **NOT** a lossless per-operation event log or a crash-safe
   write-ahead journal. It cannot replace real disk transaction
   persistence for W1 cold paging.
4. Failed sync can already have applied some ledger mutations
   before raising an exception. Logical group transfers are
   atomic per group, but the whole tick is not a full
   exception-atomic ECS transaction.
5. A bounded 65,536-entry dirty map can reject an extreme
   burst. Producers intentionally fail closed; the next stage
   must define a deterministic capacity policy rather than
   silently dropping notifications.
6. Add a deterministic dirty/movement event layer at each
   authoritative component owner, with reliable save/rollback
   restoration and group barriers, then remove the old scan
   **only after** 100-crossing/replay parity acceptance.
7. Implement lossless per-region disk transaction journal
   with checksums, crash-restart recovery and bounded checkpoints;
   then explicitly dormancy-safe AI/VehicleDrive system
   participation, cold clocks and eviction.
8. Update the catalog/skill and create another individual
   stage handoff with actually executed tests.

Status: PR #116 Draft; W1 partial. No release/merge claim.
