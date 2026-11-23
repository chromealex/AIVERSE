---
name: aiverse-region-lifecycle
description: Use the AIVERSE shared World RegionEntityLedger/RegionEcsBridge, canonical host positions, bounded region change pages and authoritative hot/cold or move admission safely.
---

# AIVERSE — world region lifecycle

Use this skill when modifying or consuming real-world ECS regional ownership, stable identity, streaming/teleport admission, or region delta delivery. **This is a generic engine capability, not an FPS/RTS subsystem.** Do not duplicate transport, physics, NavMesh, or animation owners.

## Locate the current APIs

- `engine/world/RegionEntityLedger.h` — persistent stable IDs, owner Cells, durable tombstones, group transfers, `liveIds()`, revision-indexed `since()` and bounded `pageSince()`.
- `engine/world/RegionEcsBridge.h` — `RegionStableIdentity`, binding and crossing detection through a canonical `RegionPositionReader`; do not synthesize redundant `Transform` for existing character or vehicle positions.
- `engine/world/VehicleRegionTransfer.h` — complete existing `VehicleDrive` + seat/driver/passenger group validation and atomic mixed-origin owner transitions.
- `engine/world/RegionColdStore.h` and `RegionLifecycleRuntime.h` — opt-in component dormancy and gameplay-owned hot/cold activation; **not automatically safe for all live gameplay component owners**.
- `engine/world/RegionMoveRequests.h` — bounded persisted staging queue. `ActorHost::requestRegionMoveByStableId` is a **trusted** C++ gameplay API, not a client permission check.
- `engine/actor/ActorRegionOwnership.cpp`, `ActorRegionAdmission.cpp`, `ActorStreaming.cpp` — actual world-space host opt-in via `rules.regionOwnership={}`. Existing CharacterBody/Vehicle component data remains canonical.
- `docs/region-entity-ledger.md`, `docs/open-world-framework.md` and the stage-specific handoffs under `docs/agent-handoffs/open-world-stage*.md`.

## Consume changed region state incrementally

```cpp
const auto* ledger =
    sim.world().resourceConst<aiverse::world::RegionEntityLedger>();
aiverse::world::RegionChangeCursor cursor;
const bool caughtUp = cursor.consume(*ledger, 128,
    [&](aiverse::world::RegionEntityId id,
        const aiverse::world::RegionEntityRecord& latest) -> bool {
        // Return true only when the consumer has accepted this value.
        return deliverLatestRegionState(id,latest);
    });
```

- `pageSince(revision,limit)` gives ordered current records,
  `nextRevision`, and `more`. Use a bounded limit and an
  acknowledged consumer. No full historical scan on each observer update.
- The revision index and live-ID index are **derived**, rebuilt on
  native ECS load and stored with World snapshots. They do not change
  the persistent Archive schema or authoritative checksum.
- Each stable ID has ONE latest record. A→B→C before a poll emits C
  only. **Never use this API as a lossless event history or durable crash
  journal.** To implement a real disk journal, record each transaction
  with a stable sequence/checksum before commit, then checkpoint and
  crash-recover it. Do not equate `pageSince` with WAL.
- After a World rollback or deserialize, reset derived consumer cursors
  and republish a bootstrap snapshot. Revisions may repeat on a
  divergent rewind branch.
- Never use render observer/visibility state to update an
  authoritative `RegionEntityLedger`.

## W1.15 dirty lifecycle observations

Use `engine/world/RegionDirtyQueue.h` for deterministic, bounded,
coalesced, persisted `Spawn`/`Move`/`Retire` notifications.
Participating authoritative owners call:

```cpp
actor::host(sim).reportRegionChange(
    sim,entity,aiverse::world::RegionDirtyKind::Spawn);
// Also Move after an authorized canonical position update,
// or Retire before destroying a durable actor.
```

The queue is opt-in with `rules.regionOwnership={}`, has a maximum
of 65,536 distinct pending IDs and is acknowledged only after a
successful `ActorHost::synchronizeRegionOwnership`. Its bits
coalesce: **it is not a lossless event log**. The host still performs
a full tracked-pool scan because older systems directly write
component positions. Do not remove that fallback without proving
complete producer coverage and replay/COW persistence semantics.
Do not use dirty marks as permission to teleport or as a substitute
for the pre-tick `requestRegionMoveByStableId` admission boundary.

`region_dirty_queue_test` and `actor_region_lifecycle_test` contain
authored regression assertions for the API but have not been executed
on the required Mac/KINGSTON checkout.

## W1.16 report only real cell crossings

For position-owning actor systems, use the shared
`ActorHost::reportRegionMoveIfCrossed(sim,entity)` **after** the
existing controller has updated its canonical ECS component.
It computes the current 32-unit region and compares it with the
persisted ledger owner; no dirty entry is created for motion
within the same region. The queued `Move` is acknowledged by
`actor.regionOwnership` after the normal atomic group
validation. It never changes the destination or bypasses
`readyToStep` collision admission.

Existing producers include actor motor/locomotion, Royale
skydiving, arcade vehicle movement/enter/exit, typed
`VehicleDrive` physics and seated passengers, race
respawns and bomb/round resets.

```cpp
// Example inside a trusted canonical movement owner after it moves:
if (!actor::host(sim).reportRegionMoveIfCrossed(sim,entity))
    throw std::logic_error("world region dirty notification failed");
```

Unopted worlds are no-ops. The opt-in host fails closed on
missing live stable identity, missing canonical position or a
full dirty queue. These producer hooks are NOT permission
checks or a replacement for staged teleport admission.

Keep the old full tracked-pool reconciliation until every
direct field writer has been covered and tests demonstrate
save/replay/COW parity. The extended
`actor_region_lifecycle_test` source checks boundary-only
coalescing and rollback; **it has not run on Mac/KINGSTON**.

## W1.17 complete grouped dirty mark preflight

The generic `RegionDirtyQueue` offers
`canMarkMany(std::span<const Entity>)` and
`markMany(std::span<const Entity>,RegionDirtyKind)`.
Use them when a game action moves multiple authoritative entities
together (vehicle, driver, occupants). Preflight the entire group
**before** modifying physical positions; then mark it with a single
transactional batch. Duplicate keys are coalesced and only
new missing entries allocate memory. A malformed kind or full
pending queue must return false without changing entries.

The existing ActorHost staged region move uses this contract.
The remaining motor-owner audit added `Action::Vault` and
deathmatch-respawn crossing reports. No changes to how the
shared physics, controller or seat code computes positions.

Source regressions: `region_dirty_queue_test` tests a full
65,536-entry queue and atomic refusal/coalescing, and
`actor_region_lifecycle_test` tests deathmatch respawn
from another cell. **Native CTest has not run yet.**

## ECS/movement safety

The generic ECS currently does NOT emit a full change event when
`CharacterBody.pos` or `Vehicle.pos` is mutated through a pointer.
The W1 world-space host still scans active ECS components to
find movement/new entities and uses car/occupant grouping. Do not
remove these safety scans until every authoritative creator,
destroyer, and mover participates in an explicit dirty/event
protocol with COW and rollback coverage.

`requestRegionMoveByStableId` requires a live materialized ECS actor
and an authorized call site. It stages motion for a subsequent
`ActorHost::readyToStep` collision/NavMesh gate; it does not grant
network privileges or allow unsupported direct coordinate writes.
Typed `VehicleDrive` teleports must use the existing shared
controller and seat-owner code. Never park those core gameplay
components unless every owning system is dormancy-aware.

## W1.18 native test gate and archive format

The source repository provides
`.github/workflows/open-world-w1.yml`: a focused macOS 15
GitHub Actions workflow to compile and run all 11 W1 CTests.
It installs the real external `mushrooms` template and
archives configure/build/test logs. You may run it manually
through **GitHub Actions → Open world W1 native acceptance →
Run workflow**, selecting this feature branch. Do not report
the workflow passed until an actual run shows a successful
conclusion and all focused tests are green.

For the portable `RegionDirtyQueue` Archive, each pending
entry is **9 bytes** (uint64 key + uint8 mask). The reader
must use `Archive::count(...,9)`; using 17 corrupts
valid many-entry save reloads even though the serialized
byte layout remains identical. The
`region_dirty_queue_test` contains a 1024-record roundtrip
to guard this contract.

A hosted runner is not the owner's writable
`/Volumes/KINGSTON` Mac installation. Real acceptance must
also reconcile current main, run the merged-tree quick suite
and record exact platform and build SHAs.

## Tests, acceptance and handoffs

Relevant source targets include `region_entity_ledger_test`,
`region_ecs_bridge_test`, `region_cold_store_test`,
`actor_region_position_test`, `actor_region_lifecycle_test`,
`actor_region_admission_test`, and
`race_region_respawn_gate_test`. These tests were **source-authored
but not executed** during W1 stages 01–14 from this environment.

Compile and run on the owner's macOS worktree with build outputs on
`/Volumes/KINGSTON/AIVERSE-builds/...`, then the merged-tree
`./run-tests.sh --quick` before proposing merge into main.
Report exact test results, failures, branch/commit and limitations.
Every completed stage requires its own handoff document with changed
files, API, real validation and precise next action. Keep PR #116
Draft and W1 partial until actual acceptance succeeds.

## W1.20 — change tracking, transactions and diagnostics

Use World::trackComponentChanges<T>(), notifyComponentChange<T>(entity), peekComponentChanges<T>() and drainComponentChanges<T>() at quiescent barriers. Mutable World access is conservative, not proof of value change. Raw Pool access and escaped references need explicit notifications. ComponentChangeBatch is bounded at 65536 handles; overflow requires full reconciliation. GuardedWorld adaptive jobs publish lane batches; other schedulers are not automatically covered.

RegionEntityLedger::Transaction is an exclusive sparse undo transaction: use its mutation methods and commit only after validation. No observers, snapshots or archive while active. COW detachment can still copy the resource. RegionDirtyQueue is persistent and coalesced, not a WAL. Restore hooks invalidate transient actor journals and seat cache.

Read engine/world/RegionOwnershipDiagnostics.h via World::resourceConst<RegionOwnershipDiagnostics>() for last successful reconciliation. Experimental stream manifest flags regionDirtyOnlyLoot, regionDirtyOnlySolo and regionDirtyOnlyVehicles default off; configuration and native acceptance are unverified. Vehicle mode still scans canonical groups; do not claim scan-free transport. strictRegionJournalAudit is a development coverage check, not proof of all writers. Keep full scan default. See stage20 handoffs for limitations. No W1.20 native tests have been run.
