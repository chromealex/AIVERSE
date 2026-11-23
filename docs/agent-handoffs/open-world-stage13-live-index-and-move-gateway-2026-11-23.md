# Open World Framework — W1 stage 13: live-identity index and trusted movement gateway
Date: 2026-11-23
Repo: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
PR: https://github.com/chromealex/AIVERSE/pull/116 — Draft
W1 / OW-01: **partial**, not merged / released
Prior: `docs/agent-handoffs/open-world-stage12-budgeted-respawn-2026-11-23.md`

## Scope and architecture reviewed

The user asked to continue W1 after Stage12 (bounded race collision admission). Stage13 targets two gaps:
(1) repeated scanning of all historical tombstones during world-space region reconciliation and
(2) an authoritative game/script/native C++ call site that does not need to retain transient ECS Entity handles.

Source contracts inspected on GitHub: `RegionEntityLedger.h`,
`ActorRegionOwnership.cpp`, `ActorRegionAdmission.cpp`, actual
`ActorHost` public methods, `GameCore::applyCommand`,
`HostSystems`, and `NativeModules::command/tick`. World-space
`GameCore` delegates commands to `ActorHost`. It does not
automatically load grid-only `NativeModules` or script VM
callbacks. This stage therefore adds a *trusted engine API* rather
than inventing an unauthorized network command.

## Changes authored and committed

1. `engine/world/RegionEntityLedger.h`: added an ordered, derived
   `std::set<RegionEntityId> liveIds_`; public `liveIds()` and
   `liveCount()`; maintained on `spawn` and `despawn`.
   Native ECS/Archive decoding builds a local set of live IDs in
   lockstep with decoded records, then swaps it on successful
   transactional decode. Historical tombstones still remain
   authoritative in `records_`, but are never present in
   `liveIds_`. The index is **not separately saved or hashed**,
   preserving schema 1 and existing serialized byte order.
2. `engine/actor/ActorRegionOwnership.cpp`: the tombstone
   reconciliation pass now snapshots and iterates only
   `ledger->liveIds()` instead of `ledger->records()`. This
   avoids iterating old tombstones on every game tick and works
   with existing ECS COW/rollback. It still scans live actor
   pools and checks live identity sets; it is not yet a
   fully event-driven open-world index.
3. `engine/actor/ActorModule.h` / `ActorRegionAdmission.cpp`:
   new `ActorHost::requestRegionMoveByStableId(Simulation&,RegionEntityId,
   Vec3,std::string*)`. Checks nonzero W1 stable ID, live
   `RegionEntityLedger` entry, generation-aware materialized ECS
   Entity (the existing ActorHost `Entity::packed()+1`
   identity mapping), and a matching `RegionStableIdentity`
   component. Calls the existing `requestRegionMove`
   afterwards, preserving same staged command queue,
   authoritative collision/NavMesh admission, and occupied
   vehicle group movement. Rejects tombstones/recycled
   references without changing queue, RNG or actor position.
   **The game/script layer must authorize the action itself.**
4. `tests/world/region_entity_ledger_test.cpp`: added
   live-index invariants, 2000 spawn/despawn tombstones,
   a single subsequent live ID and save/reload equality.
5. `tests/world/actor_region_admission_test.cpp`: added
   `testTrustedStableIdGateway` for actual world-space
   `GameCore`/ActorHost: invalid stable references leave
   checksum and queue unchanged; valid stable ID stages
   and applies through the existing fixed-tick movement gate.
6. `docs/region-entity-ledger.md`: Stage13 contract,
   limitations, test status and usage.
7. This independent handoff.

## Commit history

- `73d4d660f81a962d14147f02899ac5d5db14b7bb`
  — ledger live-ID index with archive rebuild.
- `c0a5eda809d7d0e424ae950bbde3db0471fec431`
  — no historical tombstone scan in ActorHost.
- `5c480f8674ba96f9c3f5a7464e745b9538d564c8`
  — stable-ID movement gateway declaration.
- `1ddf38d74d131ccd827dfed9b9a0e201ad019670`
  — implementation in existing W1 admission.
- `4ca7a58f9e47cb24567eea9738c1d3578bd0e7cf`
  — live-ID index persistence and tombstone test.
- `62ec2b9bdfb47d2faf48e1465b6fb6a8ea08dc39`
  — trusted script/native stable-ID test.
- `fb3be5a3d8d205fd07ed610530cd0bcd7832a089`
  — public API documentation.

## Tests — source assertions vs actual execution

**Authored:** the existing `region_entity_ledger_test` and
`actor_region_admission_test` CTests were extended. No new
target required. The test sources verify ID index consistency,
two thousand historic tombstones, archive rebuild, invalid ID
refusal and valid one-tick move staging.

**Actually executed:** no CMake compile, no CTest, no
`./run-tests.sh --quick`, no macOS/KINGSTON build, no native
gameplay recording, no p95 memory/performance numbers. Only
GitHub source inspection, file updates and successful commit
responses. Do not report PASS or merge PR #116 based on this
source work.

## Explicit limitations

- This index improves *only* the historical tombstone part of
  per-tick work. It remains O(live registered actor count) for
  reconciliation, plus scans of CharacterBody/Vehicle/Loot
  ECS component pools. For many millions of hot entities this
  still requires event-based spawn/retire and transform dirtiness.
- No on-disk region journal, bounded eviction or offscreen
  cold-region game simulation has been delivered.
- The stable-ID API is a trusted C++ host boundary; no new
  script DSL, network protocol or automated world-space
  Lua/native integration was added. A real game's server
  must validate permissions/mission state and register its
  script/native callback before calling it.
- The ActorHost identity mapping `Entity::packed()+1` is
  scoped to current world-space live materializations.
  Future disk eviction will need a separate stable ID→Entity
  reification index; never map an evicted ID directly to a
  stale Entity handle.
- Direct coordinate assignments outside the staged movement
  API may still bypass collision admission, and typed
  VehicleDrive/seat lifecycle must not be parked without
  safe dormant component owners.
- The feature branch remains behind the concurrently edited
  main branch. Reconcile on a Mac integration worktree
  before attempting to merge, and record actual build
  and test failures rather than assuming green results.

## Next stage W1.14

1. On macOS with writable `/Volumes/KINGSTON`, sync and
   reconcile main and feature branch, compile all W1 CTest
   targets and the complete `./run-tests.sh --quick`.
   Fix actual native compile and behavior errors first.
2. Build a shared world-entity change journal / event based
   spawn and retirement subscription instead of per-tick
   pool scans, with stable generation-aware identity and
   in-world mixed car/occupant group semantics.
3. Route any remaining server-side script/native teleport
   sources explicitly through `requestRegionMoveByStableId`
   and ensure no unauthorized direct coordinate writes.
4. Implement durable per-region disk journal and recovery;
   then explicitly dormant-aware AI/VehicleDrive owners
   and cold-population progression.
5. Include actual compile/CTest evidence and an independent
   handoff after Stage14. Keep PR Draft until acceptance.

No W1 production completion or release claim.
