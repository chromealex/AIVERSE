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

## Persistent identity allocation (W1.24c)

Use `RegionEntityLedger::nextIdentity()` to allocate a nonzero monotonic ID at the authoritative spawn/bind barrier. It derives the high-water mark from remaining records and the persisted sealed identity prefix. Zero means exhausted. Never derive a stable ID from Entity::packed(), index or generation; old stored stable tags remain valid.

ActorHost assigns new identities in semantic order (character slot, vehicle/loot object ID), keeps existing tags and maintains a derived bidirectional runtime-handle/identity index for destruction notifications. Clear the index on restoration; full reconciliation remains the default. The experimental candidate-only path requires a valid prior index and complete notifications.

Use `PersistentEntityReferences::rebuild` and `resolve` to translate stable IDs to current live handles. RegionMoveRequests v2 persists stable IDs; v1 ActorHost keys equal the historic stored tags and migrate through the registry with binding validation. DirtyQueue keys are runtime observation handles plus one, NOT persistent IDs; drain them before portable restart and never interpret them as ledger identity.

Production portable actor restoration still requires explicit component/resource codecs and materialization policy; do not enable it solely because region ownership and movement addressing now use stable IDs.

## Tombstone compaction (W1.25d)

RegionEntityLedger v2 preserves two watermarks: retiredFloor seals the identity prefix, historyFloor is the latest removed record revision. Missing IDs at or below retiredFloor are permanently unavailable (including previously unused gaps), but existing live records remain valid. nextIdentity uses the maximum of the sealed prefix and remaining records. Native resource schema v1 is explicitly migrated; the ledger archive reader also accepts v1. Old binaries cannot read v2.

Only compact at a quiescent checkpoint barrier on a private candidate. UnifiedWalSession::checkpoint/queueCheckpoint accept optional compactThrough and compactLimit; zero cutoff disables compaction. The new ledger is published only after checkpoint/WAL rotation succeeds, and the authority adopts the identical compacted ledger before another tick. This does not increment gameplay revision or WAL sequence. Do not mutate the live ledger independently. No ID reuse or standalone compaction WAL frame.

Before delivering incremental changes, check ChangePage::resetRequired or RegionChangeCursor::requiresBootstrap. If stale, replace the observer's projection from a full live snapshot and reset its cursor to that snapshot revision; do not just advance the cursor. since() refuses compacted history. PersistentEntityReferences classifies missing sealed IDs as retired. Older backup generations preserve their own ledger state; a fork must use its own world identity.

Server checkpointWorld(retainRevisions) defaults to 65536 recent revisions. CLI --world-retain-revisions N sets the window at configured --world-checkpoint-ticks rotations; 0 removes all eligible tombstones, UINT64_MAX disables compaction. Configure periodic checkpoints for high-churn worlds. The per-ledger record cap still applies between rotations; live entity cap, full ECS snapshot memory and disk retention of old checkpoint/WAL generations remain separate concerns. No native performance or long-duration acceptance has run.

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
of 65,536 distinct pending runtime handles and is acknowledged only after a
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

MCP: call `region_ownership_diagnostics {}` in a running editor play session. It returns `available:false` when no live resource exists; it never fabricates headless metrics or mutates simulation. Native MCP build not yet verified.

## W1.28 — authoritative decision LOD (source implementation)

Use `engine/world/SimulationLod.h` for a genre-independent, deterministic
decision cadence. The opt-in root rules block is:

```json
{"simulationLod":{"enabled":true,"hotCells":4,"warmCells":16,"warmTicks":4,"coldTicks":16}}
```

The shared policy takes current authoritative Cell, a span of authoritative
observer Cells, nonzero stable identity, fixed tick and a critical-work
override. Missing observers/identity and invalid configuration fail hot.
The actor adapter uses 32-metre cells and existing BotMemory/CharacterControl;
only idle/patrol decision work is decimated. Active combat/interaction,
nearby actors, loot/vehicles, interiors and live projectiles force the normal
path. Motor/physics, damage, weapons, ownership and WAL still tick normally.
TrafficPopulation reuses the fair slice formula without changing its current
two-second population check window. This is NOT actor/vehicle cold parking,
unloaded-world advancement, or a global AI scheduler.

Read authored settings using `world_simulation_lod_get`; validate with
`world_simulation_lod_validate {config}`; save using
`world_simulation_lod_set {config,expected}` (expected is the previous normalized
five-field config). The setter uses the existing rules owner and refuses an
occupied maintenance slot. The Studio World Persistence Simulation LOD tab
uses those same operations and is project-bound. Stop the server before
changing rules; existing persistence content identity may no longer match.
No live configuration migration is implied. Last-pass diagnostic counts are
derived ECS resources, excluded from durable projection, cleared by the
region restore hook. Read them through the live editor world_regions_status
simulationLod field; absence is unavailable, not zero measured work.
No build, test, performance measurement or cross-host parity has been run.

## W1.29a — explicit dormant-component catch-up (native source)

Use the existing `RegionDormancyController::advanceGroup<T>` for a group of
parked ECS entities that all own the named, registered component. Its
portable component codec must already include the owner's last simulated
tick. Supply the actual authoritative target tick; the controller does not
own a separate clock or read wall time.

```cpp
// NeedsState is an example of a domain-owned, portable component.
// Every member must be dormant, registered and in the expected Cell.
const auto clock = [](const NeedsState& s) -> uint64_t { return s.lastTick; };
const auto catchUp = [](NeedsState& s, world::RegionEntityId id,
                        uint64_t from, uint64_t to) -> bool {
    return advanceNeedsDeterministically(s, id, from, to);
};
if (!controller.advanceGroup<NeedsState>(ecs, ledger, cold, group,
        cell, "myproject.needs", authoritativeTick, clock, catchUp, &error)) {
    // No partial cold-store update; handle refusal through the owner.
}
```

The domain policy updates the component's lastTick to the target and must
be pure, bounded, deterministic and preserve references/invariants. The
controller checks monotonic time, stable IDs, owner revisions, codec
version/type, duplicate members and serialized byte limits. Same tick is a
no-op. The target timestamp must roundtrip through the codec. Stable-ID
order is independent of input order. It never creates or destroys entities,
moves positions, runs physics or calls an offscreen traffic solver.

For a single clock-bearing component type, use
`advanceAndWakeGroup<T>(ecs, ledger, cold, group, cell, component,
targetTick, clock, catchUp, admit, &error)`. The read-only `admit`
predicate accepts `(const World&, Cell, std::span<const Entity>)` and
must verify all required host readiness and linked group eligibility.
The caller supplies the complete linked group. No speculative catch-up
is published if admission or wake fails. Other parked component types
must be safe to pause; a multi-type transaction is not provided yet.
Do not use ordinary wakeGroup to bypass catch-up of opted-in components.

Existing cold-store Archive schema 1 is unchanged. No new rules block,
MCP command, Studio control, automatic actor/vehicle parking, or true
entity absence. Candidate copy/audit costs scale with total parked
payload; not an every-entity/every-tick fast path. Native tests and
cross-host acceptance NOT RUN. See
`docs/agent-handoffs/open-world-stage29a-cold-advancement-2026-11-24.md`.

## W1.29b — complete cold plan and guarded park/wake

Prefer the complete plan for new native hosts. Register each parkable ECS type
once under its portable name/version, then explicitly mark each type as safe to
pause or provide a portable clock and deterministic catch-up policy. A plan may
cover heterogeneous groups, but every parked component needs a policy.

```cpp
world::RegionDormancyController::ColdAdvancePlan plan;
plan.addAdvance<NeedsState>("myproject.needs",
    [](const NeedsState& s) -> uint64_t { return s.lastTick; },
    [](NeedsState& s, world::RegionEntityId id, uint64_t from, uint64_t to) {
        return advanceNeedsDeterministically(s, id, from, to);
    });
plan.addPaused<StaticProfile>("myproject.profile");
// Both names/types must match controller.registerComponent<T>().
controller.parkGroupGuarded(ecs, ledger, cold, group, cell,
    authoritativeTick, plan, mayPark, &error);
controller.advanceGroup(ecs, ledger, cold, group, cell,
    authoritativeTick, plan, &error);
controller.advanceAndWakeGroup(ecs, ledger, cold, group, cell,
    authoritativeTick, plan, mayWake, &error);
```

Check every return value and handle failure at the authoritative owner.
Admission predicates receive a read-only World, Cell and stable-ID ordered
span of entities. The owner must check complete linked groups, pins, active
actions, references, navigation/collision readiness and all live consumers.
Clocked components must encode their current authoritative tick at parking.
Catch-up policies must be copyable, pure, bounded, independent per component,
and preserve domain references/invariants. The shared controller orders work
by stable ID and codec name, validates all blobs before catch-up, checks
portable clock roundtrip and stages one candidate before publishing. Unknown
or missing policy refuses the whole complete-plan operation. Same-tick
advancement does not write the store. Existing pause-only and single-type
APIs are not a way to bypass a required complete policy. No automatic host
integration, true absence or performance claim yet. Schema 1 unchanged.
Native builds and tests NOT RUN; see stage29b handoff.
## W1.30a — reuse the existing population owner

Traffic's opt-in virtual trip deadline catch-up (root simulationLod.enabled)
uses its own existing persisted TrafficCitizen records and clocks, not
RegionColdStore copies of those records. See the city-traffic skill and
docs/traffic.md. Do not park occupied vehicles or duplicate population
identity. Generic guarded RegionLifecycleRuntime queue integration and
host-specific park eligibility are separate follow-up work. NOT RUN.
