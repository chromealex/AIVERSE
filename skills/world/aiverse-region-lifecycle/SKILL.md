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
