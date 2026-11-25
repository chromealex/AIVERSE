# AIVERSE Open World — W1 stage 16: canonical movement owner dirty notifications

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
System: W1 / OW-01 — **partial / not Mac-verified**
Previous: `docs/agent-handoffs/open-world-stage15-dirty-lifecycle-2026-11-23.md`

## Goal, architecture and source evidence

W1.15 introduced a persisted `RegionDirtyQueue` for opt-in,
deterministic coalesced Spawn/Move/Retire observations. It already
tracked loot/car structural actions and explicitly staged teleports,
but ordinary character motor movement, Royale skydiving, and real
typed `VehicleDrive` movement updated their canonical positions
without signaling a boundary crossing.

Read current source contracts before editing:
`engine/actor/ActorModule.cpp` normal motor/locomotion,
`ActorSystems.cpp` arcade vehicles and bomb respawns,
`ActorVehicles.cpp` shared typed driving via
`VehicleWorld::placed`, race checkpoint respawns and seat
entry/exit, `ActorRoyale.cpp` skydiving,
`ActorRegionOwnership.cpp` coalesced dirty consumer and
legacy scanning fallback, and `ActorModule.h` W1 API.
No new driving, physics, seat or movement algorithm was created.

## Changed files / API

1. `engine/actor/ActorModule.h`: public
   `bool reportRegionMoveIfCrossed(Simulation&,Entity)`.
2. `engine/actor/ActorRegionOwnership.cpp`:
   - Read-only lookup of the existing RegionEntityLedger,
     RegionStableIdentity and the canonical `RegionPositionReader`.
   - Compute the current cell with the existing
     `SpatialWorld(32)`; if it matches the stable ledger
     owner, return true without writing any persistent queue.
   - If different, delegate to `reportRegionChange(...,Move)`.
     Repeated calls coalesce in the existing bounded queue.
   - Unopted games return true without new persistent resources.
     Opted-in missing identity/position or out-of-budget dirty queue
     returns false, so owning systems can reject rather than
     silently lose a boundary notification.
   - The existing post-tick `synchronizeRegionOwnership`
     still validates all car/occupant graphs, migrates owners
     and ACKs dirty entries after completion.
3. `engine/actor/ActorModule.cpp`:
   - After normal `moveActor`/`moveActorModes` position updates
     for on-foot characters.
   - After original arcade vehicle reset to its starting pose.
4. `engine/actor/ActorRoyale.cpp`:
   - After authoritative skydiver `CharacterBody.pos` change.
5. `engine/actor/ActorVehicles.cpp`:
   - After `VehicleWorld::placed` updates real `Vehicle.pos`
     and each typed seated `CharacterBody.pos`.
   - After `RaceSeats::respawned` updates a real typed
     vehicle and its passengers from a shared checkpoint.
   - After actual typed seat exit assigns the canonical
     passenger exit pose.
6. `engine/actor/ActorSystems.cpp`:
   - Arcade driver enter, exit and vehicle/driver movement.
   - Bomb round respawn after character snapshot assignment.
   - Existing `vehicleEntity(sim,a.vehicle)` resolves car
     Entity; a transient undefined variable introduced during
     stage editing was corrected before the stage handoff.
7. `tests/world/actor_region_lifecycle_test.cpp`:
   - New `testBoundaryOnlyDirtyMotionProducer`: same-cell
     movement creates no dirty queue entry, cell crossing
     emits one bitmask even after duplicate reports, normal
     region reconciliation migrates and ACKs, repeated
     settled reports remain no-ops, and COW snapshot/restore
     reverts region and checksum.
   - Existing 100 occupied-car crossing, dynamic loot
     spawn/retire, tombstone and peer parity source tests retained.
8. `docs/region-entity-ledger.md`: detailed Stage16 owner
   contract and noncoverage.
9. `skills/world/aiverse-region-lifecycle/SKILL.md`:
   - Documented boundary-only producer calls, covered owners,
     COW and pre-tick collision admission contract.
10. This independent handoff.

## Commit trail

- `44fcbc6143ee46c7f58e1a5beea20c85c717a144` — public
  boundary-sensitive dirty report API.
- `b6fb3ff2606afd6dcf55af3327b84033e552d228` —
  canonical cell comparison and event emission.
- `f7f16af01da6e1ad417ddb78c9ca939b61e4598c` —
  normal motor/locomotion and arcade round vehicle reset.
- `66db1d04a025f17d88c61974efc3d2b1b4522e98` —
  Royale skydiver movement.
- `da6209250f64d6c02809c651c791b6f4d35b4fb4` —
  typed VehicleDrive and race respawn movement.
- `582cf5049b1da466fe6bde6e8f583f5921fb6774` —
  arcade vehicles, enter/exit and bomb respawn.
- `aa9e777a266cb1a6a8c358b003bb8078bda082b9` —
  correct arcade car identity lookup.
- `7a24c1b4745fd8f88045864fa3e64d882cf906d8` —
  typed seat exits.
- `d08c5a981411b0eed6f7708ebcdce9baefc4fd25` —
  regression source for same-cell/no-op, boundary, coalescing, COW.
- `2fb99a0f10b74c777047115943b319a37b47ac02` —
  W1.16 API documentation.
- `a753aba8822b2bd6ff79037b0ad0f62df690bafb` —
  agent skill update.

## Test execution versus source coverage

**Actually performed:** Read code contracts through connected GitHub
and committed source/test/docs/skill changes. Source files have
been inspected and verified in the feature branch.

**NOT performed:** Native compiler, CMake, CTest,
`./run-tests.sh --quick`, merged-tree Mac/KINGSTON acceptance,
performance profiling, save/load corruption runner or gameplay
capture. This runtime has no writable user's
`/Volumes/KINGSTON/AIVERSE-builds` macOS volume. Do not claim
any new test PASSED, release readiness or safe merge.

## Remaining gaps / next W1.17

- The dirty queue is still only **partially complete**:
  arbitrary native/script field writes, third-party ECS component
  owners, project-specific NPCs and direct Transform assignments
  are not all instrumented. The legacy full scan MUST stay enabled
  to retain correctness.
- A movement into a new region is reported **after** canonical
  movement. Producer signaling is not a pre-tick collision
  admission and cannot authorize an unsafe teleport. All
  discrete large jumps must still use
  `requestRegionMoveByStableId` / `requestRegionMove`.
- System callbacks now fail closed on a full dirty queue. They
  might throw after another movement field was already changed.
  Whole-tick exception atomicity is not yet guaranteed.
- The current `RegionDirtyQueue` is coalesced, not a lossless
  per-action durable disk journal. Full-scan O(hot actors)
  processing remains. No disk checkpoint/eviction or fully
  dormant-aware AI/VehicleDrive lifecycle implemented.
- Stage17 should first reconcile branch/main, compile and run
  the real Mac CTests. Fix failures **before** more features.
- Then design event hooks around the authoritative ECS
  canonical-position owners, preserve COW/replay parity,
  make dirty batching predictable, and remove fallback scans
  only after a complete writer audit and 100-crossing tests.
- Implement a lossless disk region journal with crash recovery
  and safe actor/car cold storage after durable contracts exist.
- Write next separate handoff including actual test results,
  updated branch/SHA and explicit next stage.

Status: W1 partial; PR #116 Draft. No main merge or release claim.
