# AIVERSE Open World — W1 stage 14: revision-indexed latest-state change delivery

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
W1 / OW-01: **partial; not Mac acceptance, not merged**
Prior handoff: `docs/agent-handoffs/open-world-stage13-live-index-and-move-gateway-2026-11-23.md`

## Actual source inspection and design decision

Before adding ECS callbacks, inspected:
- `engine/core/ECS.h` `World::create/destroy/add/remove`, its
  copy-on-write snapshot semantics and `eachExistingConst`.
- `engine/actor/ActorRegionOwnership.cpp` full traversal of
  `CharacterBody`, `Vehicle` and `Loot`, with vehicle/seat group
  validation and durable tombstone handling.
- `ActorHost::tick`, `ActorVehicles` and `ActorSystems` mutate
  positions and component fields directly.
- The existing `RegionEntityLedger::since` iterated every retained
  historical record, even when a streaming observer only needed one
  small page of changes.

The generic ECS currently does not emit a per-field `onChanged`
notification on direct mutable access and changing that globally
without a full, save-safe event protocol would risk lost position
updates or invalid replay. W1.14 therefore adds a *real incremental
**region ledger output*** API first, while explicitly retaining the
ActorHost's correctness fallback full scans. Do **not** describe this
as full event-driven actor position/identity tracking yet.

## Code and changed files

1. `engine/world/RegionEntityLedger.h`:
   - New **derived** `revisionIndex_`:
     `std::map<uint64_t,RegionEntityId>`, one entry per stable ID,
     ordered by its **latest/current** ledger record revision.
   - Maintained for `spawn`, single- and multi-ID
     `transfer`, mixed-owner `transferGroup`, and
     `despawn`. The original `records_`, `liveIds_`
     and `lastRevision_` remain the authoritative state.
   - `io(Archive&,RegionEntityLedger&)` builds an independent
     ordered index and live index while decoding. On success swaps all
     decoded structures; on failure preserves original resource.
     The **schema remains v1**, no new fields serialized or hashed.
   - `since(revision)` now starts with
     `revisionIndex_.upper_bound(revision)` and returns current
     records in revision order without an O(all historical entities)
     scan and sort.
   - New `ChangePage pageSince(uint64_t after,size_t limit)`
     returns `changes`, `nextRevision`, `more`. Its
     memory/delivery is bounded by `limit`, suitable for
     incremental network/view observers and future checkpoint deltas.
   - New `RegionChangeCursor`: derived host-side observer with
     `consume(ledger,budget,callback)`, ACK per entry, retries
     callback rejection without losing the position, and
     `reset()` for rollback/restore. The cursor is **not**
     a gameplay-authoritative or persisted resource.
   - Safety fix: `spawn` rolls back newly allocated index/record
     structures on exception; transfer and tombstone update
     `revisionIndex_` by re-keying a node handle without
     allocating another map entry.
2. `tests/world/region_entity_ledger_test.cpp`:
   - Added sample of 80 region records, 60 transfers and 15
     tombstones; checks that successive 7-record pages exactly
     reproduce `since(0)` in current-revision order.
   - Archive read/write rebuild produces identical pages, old
     schema bytes and record revisions.
   - Cursor tests bounded 5-record delivery, handler false on one
     entry and exactly-once successful ACKs thereafter, explicit
     cursor reset, zero budget and caught-up behavior.
   - Existing W1.1 tests for mixed car/occupant transfer, 100
     crossings, COW rollback, malformed archive and live-ID index
     remain in the same target.
3. `docs/region-entity-ledger.md`:
   - Stage14 public contracts, usage and strict latest-state
     vs lossless-journal distinction.
4. This agent handoff.

## Commits made in this stage

- `373e490f5f0386b2504a13d42365697d1bc555b0` — revision
  index and bounded latest-state paging.
- `2ef408b3d307da91d0def39f2415e6c157c65af9` — pagination
  archive and mixed-transfer tests.
- `a2d9696a6efbd1f96a393ef00f944e3bded5458e` — ACK cursor
  observer API.
- `6fccbacaa3547ec02cd529178811ef65486eb699` — callback
  failure/retry tests.
- `5b58a804661e218e2bdf296ef7a666466a9069f4` — Stage14 docs.
- `364350a1b8df890ed2cc8c4c816f284c9fb9a0c4` — allocation
  rollback and map node re-keying.
- `75bb41a61469a12a56a1e15e8f5f55e403d850fe` —
  exceptional mutation/doc clarification.

## API example

```cpp
const auto* ledger =
    sim.world().resourceConst<world::RegionEntityLedger>();
world::RegionChangeCursor cursor;
bool caughtUp = cursor.consume(*ledger,128,
    [&](world::RegionEntityId id,
        const world::RegionEntityRecord& current) -> bool {
        return adapter.acceptLatestRegionRecord(id,current);
    });
```

The callback is supplied by the actual consumer. `cursor.lastRevision()`
can be passed to `pageSince(cursor,budget)` directly.
Consumer must reset after restored World/rollback, as revision values
may repeat across divergent rewind histories.

**Crucial: convergence, NOT audit events.** Each ID has one
`revisionIndex_` entry for its *latest* state. If a car crosses
regions A→B→C before the consumer polls, C is delivered, not both
transfers; the same applies to a quick create/despawn tombstone.
This is correct for current state replication/index invalidation
and not correct for every-event replay. Future crash journal
must record each mutation transaction individually and commit
it atomically to durable storage.

## Tests performed and not performed

- **Performed**: read actual current GitHub source contracts and
  implementation and created source/test/doc commits. Tool fetches
  confirmed file content was accepted by GitHub.
- **NOT performed**: native C++ compilation, this
  `region_entity_ledger_test` CTest, full `./run-tests.sh --quick`,
  merged Mac/KINGSTON acceptance, disk crash/recovery testing or
  performance benchmarks. The owner's required
  `/Volumes/KINGSTON` macOS environment is unavailable here.
  The new asserts have been authored, **not proven passing**.

## Remaining W1 issues / next W1.15

1. **Mac acceptance first**. Fetch latest main, resolve concurrent
   changes into a temporary integration worktree and build the
   current W1 targets on `/Volumes/KINGSTON/AIVERSE-builds/...`.
   Run `ctest --output-on-failure` and the whole
   `./run-tests.sh --quick`; record real SHA, failures and fixes.
2. Design a **real ECS structural/movement dirty event contract**:
   capture create/destroy and authoritative canonical position
   changes at their *owning* systems; consume in stable ID order,
   preserve COW rollback and native v10 save/load. Do not simply
   remove the ActorHost full scan until the coverage of all
   legitimate motion writers is proven.
3. Use `RegionChangeCursor` for observer/index updates after
   region ledger mutations, not for a lossless disk event log.
4. Implement per-region disk journaling with append order,
   transaction IDs, durable commit/checksum and crash recovery,
   then bounded checkpoint+eviction and cold AI/VehicleDrive
   ownership with all gameplay systems aware of dormancy.
5. Run 100-crossing two-peer replay, crash-restart scenarios
   and p95/memory profiles. Add another stage-specific handoff
   with actually executed evidence.
6. Keep PR #116 Draft until full Mac quick-suite acceptance. The
   branch was over 40 commits behind main at the prior stage:
   never overwrite concurrent changes or claim a merged release.

No release-ready or production complete W1 statement.
