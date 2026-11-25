# AIVERSE Open World — W1 stage 17: missed motion writers and atomic dirty group capacity

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Pull request: https://github.com/chromealex/AIVERSE/pull/116 (**Draft**)
Area: W1 / OW-01, **partial**, not ready for a main merge
Previous: `docs/agent-handoffs/open-world-stage16-movement-owner-dirty-2026-11-23.md`

## Scope and discoveries

Stage16 placed opt-in crossing notifications in most built-in ActorHost
movement owners but retained the necessary full ECS compatibility scan.
W1.17 audited remaining direct writes and dirty-event capacity behavior
before attempting to remove that scan. Inspected source of
`ActorModule.cpp`, `ActorSystems.cpp`, `ActorVehicles.cpp`,
`ActorRoyale.cpp`, `ActorRegionAdmission.cpp`,
`ActorRegionOwnership.cpp`, `RegionDirtyQueue.h` and the existing tests.

Two *live* canonical position write paths were not notifying the region
queue:
- `Action::Vault` in `ActorHost::action`.
- Deathmatch auto-respawn `assign(a,fresh)` in `ActorHost::tick`.

Setup-time spawning/racing/air insertion happens before
`synchronizeRegionOwnership` initially tags entities, so they
do not require an already-live stable-ID movement notification.
Short-lived Projectile is deliberately excluded from durable W1
region ownership and must not create a tombstone per shot.

## Changed source and APIs

1. `engine/actor/ActorSystems.cpp`
   - After the existing collision-checked vault canonical position write,
     calls `reportRegionMoveIfCrossed(sim,a.entity)`.
   - Fails visibly on invalid/missing materialization/queue limit,
     rather than silently omitting a region-boundary notification.
2. `engine/actor/ActorModule.cpp`
   - After deathmatch `assign(a,fresh)`, calls the same boundary-only
     notification. Existing `readyToStep` already pins all `spawns_`.
3. `engine/world/RegionDirtyQueue.h`
   - Added `canMarkMany(span<const Entity>)` to preflight an entire
     group for valid generation-aware keys and bounded new ID capacity.
   - Added `markMany(span<const Entity>,RegionDirtyKind)`. New group
     keys are allocated before existing masks are modified, and
     newly inserted keys are rolled back if allocation throws.
     Existing flags update after all missing keys are present.
   - Duplicate/previously-present Entity keys require no extra slots.
     No full queue copy is made, so work scales with the moved graph.
   - Both `mark` and `markMany` reject invalid enum flag values.
     Persistent Archive schema remains unchanged.
4. `engine/actor/ActorRegionAdmission.cpp`
   - Collects all members of admitted vehicle/occupant and independent
     character moves. Preflights all dirty queue keys **before** changing
     any canonical position. Uses a single `markMany(...,Move)`
     after commit rather than potentially partially marking seats one
     by one. Existing pre-tick collision/NavMesh admission remains
     unchanged.
   - This reduces queue-overflow partial notifications, but it is
     not an all-component exception-atomic movement transaction.
5. `tests/world/region_dirty_queue_test.cpp`
   - New batch-capacity scenario fills all but one of 65,536 slots,
     verifies that two new group members fail without modification,
     checks repeats/existing ID coalescing and invalid kinds.
6. `tests/world/actor_region_lifecycle_test.cpp`
   - New real `GameCore` deathmatch-respawn scenario from a different
     region, checks that the actor returns to its start cell,
     dirty events are ACKed and gameplay tick progresses.
7. `docs/region-entity-ledger.md`
   - Stage17 contract, limits and unexecuted native regression status.
8. `skills/world/aiverse-region-lifecycle/SKILL.md`
   - Documents grouped dirty preflight and safe owner code.
9. This independent handoff.

## Commit trail

- `083543a39796485b2f1c8ac316b12944212c33b0` —
  vault boundary notification.
- `65ac67387f9b64f8205bb16c94dd288b592a9810` —
  deathmatch respawn boundary notification.
- `3f816105c316beb5d728d38b8574d21c847015f3` —
  batch capacity API.
- `fd8a8db64abb9d3509c27fef662c85de38dce53f` —
  staged move group preflight and single dirty batch.
- `36a62342aa1c4e6939b3806c917298c94969383f` —
  overflow/coalescing assertions.
- `a159e3928cb7950662e32aee59204d26e7644f05` —
  avoid copying entire 65k pending queue on every group.
- `e310bea532aa3a9cdfffe854eb3131d3edf82969` —
  real deathmatch respawn test source.
- `dd9892f578f0af0444ef802b262004ea8d051a3c` —
  invalid enum kind validation.
- `f966853ad02103939eed64cb00fa0803614301ca` —
  invalid kind test.
- `094b776079c4e089ab3474a2aff75228c4988267` —
  stage17 public API docs.
- `a5dfa20bbe4076d5a8abab861e3f10e1800c69f8` —
  skill update.

## Tests and actual evidence

**Performed:** fetched source contracts from the actual GitHub branch,
authored and committed changes, regression sources and docs. No
local machine build or test result is claimed.

**Not performed:** no native C++ compiler, no CMake configuration,
no CTest executable, no `./run-tests.sh --quick`, no macOS or
`/Volumes/KINGSTON` acceptance. Neither batch dirty regression nor
deathmatch respawn test has been executed here. No gameplay capture,
two-peer recording or throughput data was captured.

**Critical:** The W1 branch has accumulated many stages of uncompiled
source changes and differs materially from concurrent main development.
Treat all newly authored test assertions as unverified; fix compile
and runtime failures before accepting W1 or merging main.

## Remaining limitations and next W1.18

1. First establish a real Mac/KINGSTON build and run all W1 targets
   (region ledger/bridge/cold store, ActorHost position/lifecycle,
   movement admission, race and dirty queue). Merge latest main into
   a disposable integration worktree and fix actual conflicts.
2. Run the complete merged-tree `./run-tests.sh --quick`,
   report real pass/fail counts and any COW/persistent ECS issues.
3. Extend owner coverage to game/native/project-created entities,
   and prove event parity against the compatibility scan across
   100+ car/driver/passenger transitions and save/replay/rollback.
   Do not remove the scan based on this source-only audit.
4. The dirty group preflight prevents a known queue-budget failure,
   but unexpected allocation errors in movement components can still
   leave a partially advanced tick. A world-level structural
   transaction/abort-and-retry protocol is still necessary.
5. Implement a separate **lossless** disk region mutation journal
   (WAL, checksums, checkpoints, crash recovery), not a coalesced
   RegionDirtyQueue or latest-state RegionChangeCursor.
6. Only then implement safe dormant/offscreen lifecycle for real
   NPC and VehicleDrive gameplay components, disk paging and
   memory/performance budgets.
7. Update agent skills and the next handoff with actually run tests,
   exact branch/commit, remaining limits and next action.

Keep PR #116 Draft and W1 partial; do not merge to main or release.
