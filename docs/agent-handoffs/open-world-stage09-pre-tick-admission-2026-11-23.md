# Open World Framework — W1 stage 09: pre-tick destination admission
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
PR: https://github.com/chromealex/AIVERSE/pull/116 (DRAFT)
Area: W1 / OW-01, **partial**, not accepted.
Previous handoff: `docs/agent-handoffs/open-world-stage08-actor-reconciliation-2026-11-23.md`

## What was implemented

W1.8 followed the canonical positions of ActorHost entities **after** the gameplay tick. It could detect an unintended fast move outside loaded collision, but too late to guarantee that the gameplay commands/RNG/positions remained unchanged. W1.9 adds an explicit opt-in **pre-tick destination admission** API so authorized discrete teleports and scripted region moves do not execute until the source/destination collision residency has been checked by the existing `ActorHost::readyToStep`.

Added files:
- `engine/world/RegionMoveRequests.h`: persisted, versioned, bounded native ECS resource `RegionMoveRequests` with `map<packedEntity+1,Vec3> destinations` and applied counter. Maximum 64 pending requests; portable Archive schema v1; decode transactional. Save/load validator rejects requests to dead or recycled ECS handles before replacement of the running Simulation World.
- `engine/actor/ActorRegionAdmission.cpp`: `ActorHost::requestRegionMove(sim,e,destination,error)` and `ActorHost::applyRegionMoves(sim)`. The first stages gameplay-authorized target without changing positions. The second applies only previously admitted requests, before the regular ActorHost gameplay systems.
- `tests/world/actor_region_admission_test.cpp`: real GameCore/ActorHost collision streaming refusal/retry, peer parity, save/reload, late in-tick command, and grouped arcade-car movement.

Updated files:
- `engine/actor/ActorModule.h`: public staging API, private commit system and host-local `admittedRegionMoves_` cache of destinations verified by the most recent ready check. This cache is derived and deliberately not saved/hashed.
- `engine/actor/ActorModule.cpp`: with `rules.regionOwnership={}` register/set the new persistent resource and add `actor.regionMoves` before the ordinary world-space gameplay systems. Games without W1 opt-in are unchanged.
- `engine/actor/ActorStreaming.cpp`: reuse existing collision/NavMesh `readyToStep` machinery, adding pending destination bounds to existing interest volumes. Save the exact preflight-approved requests in the host-local cache only after both collision and derived bot navigation are ready; no ECS/gameplay writes in the readiness hook. On non-async maps, cache requests without waiting for streams.
- `engine/actor/CMakeLists.txt`: compile `ActorRegionAdmission.cpp` inside the existing `aiverse_fps` library; no second physics/character simulator.
- Root `CMakeLists.txt`: CTest target `aiverse_actor_region_admission_test`, name `actor_region_admission_test`, links `aiverse_authority`, labels World/ECS/NETWORK.
- `docs/region-entity-ledger.md`: W1.9 two-phase staging/admission/commit API and limitations.
- This handoff.

## API and lifecycle contract

```cpp
// A trusted gameplay action or validated server/native command requests this
// BEFORE moving an Entity, not from the renderer or camera.
actor::host(sim).requestRegionMove(sim,entity,{324_fx,2_fx,4_fx},&error);
// Entity canonical position remains unchanged until a later admitted step.
sim.step(); // If remote collision unavailable: returns without advancing tick.
```

- The queue key is `Entity::packed()+1`, not raw packed value: the first ECS Entity may have packed handle zero, and zero is invalid in the persistent codec.
- A request is accepted only when the entity is alive, position-bearing, stable region-tagged, not dormant, within configured world bounds, and does not overlap an already staged group.
- Supported this stage: an on-foot `CharacterBody`, or an untyped `actor::Vehicle` with its canonical `Vehicle.driver` and `CharacterBody.vehicle` occupants. A passenger cannot move separately from its vehicle. Typed `vehicles::VehicleDrive` teleports are expressly refused: the host must first implement an authoritative multi-component controller/seat pose teleport transaction, not just move the presentation pose.
- For async world maps, `ActorHost::readyToStep` appends 4-unit destination bounds for all staged requests, then invokes the *existing* `CollisionStream::prepareRegions` and derived bot navigation readiness. When any required chunk is unavailable, `Simulation::step` returns before consuming commands/RNG or changing the authoritative tick.
- Successful `readyToStep` records exactly the currently admitted `<entity,destination>` entries in `ActorHost::admittedRegionMoves_`. This nonauthoritative cache is cleared/recomputed on every ready check, including after rollback.
- `actor.regionMoves` runs before regular ActorHost projectile/movement/tick systems. It stages the complete target group, validates all pre-admitted requests and moves the canonical `CharacterBody.pos`/`Vehicle.pos` components by relative delta. It only erases/marks applied the queue entries that were **actually admitted**. The already existing `actor.regionOwnership` system publishes ledger region changes at the end of that same admitted tick.
- **Late command safety:** a GameCore/ActorHost gameplay command may stage a new request *after* `readyToStep` was called for that tick. Such a request was not admitted and is **not** executed by `actor.regionMoves` in that tick. It waits for the next ready check. A newly superseded destination is also not executed under stale admission.
- Request entries, applied count, and stable entity identity are authoritative persistent ECS state; preflight results and loaded collision/nav data are host-local derived state.

## Test coverage authored

CTest `actor_region_admission_test` is source-registered and includes:
1. Two independent real `GameCore` simulations, a hashed two-chunk `CollisionStream`, an in-range start chunk and remote requested chunk at x≈324 that deliberately fails reads, then recovers.
2. While unavailable: repeated blocked `Simulation::step`, unchanged tick, RNG, checksum, command history and canonical actor position; request remains queued.
3. On recovery: both peers process the original pending command, the destination request applies once, the simulation resumes and peer checksums match.
4. Native ECS v10 serialized state after movement, fresh-instance restore, later parity.
5. In-tick authored system stages a new movement request *after* the earlier `readyToStep`; the first tick does not apply it, the following admitted tick does.
6. A queued (not-yet-executed) destination survives native save/reload and applies deterministically afterward.
7. On non-async world-space maps a grouped arcade car with occupants moves together; overlapping individual occupant request is refused.

**Actual execution:** NONE of these new integration assertions has been compiled/run on the user’s Mac/KINGSTON build volume. Source files were inspected through GitHub and accepted as commits. No `ctest`, `./run-tests.sh --quick`, real game capture, p95, memory profile or macOS build claim is supported. Keep PR Draft, W1 `partial`.

## Limitations requiring next stage

1. **Not universal interception:** the new admission protocol protects only movements that go through `requestRegionMove`. Existing gameplay code that directly assigns `CharacterBody.pos` or `Vehicle.pos` can still teleport beyond preloaded collision. W1.8’s after-tick reconciliation may throw after part of the tick was already mutated; the whole simulation is not transactionally rolled back by this feature. Fix by routing all authoritative discrete movement/teleports through a single shared preflight protocol with owner involvement.
2. Typed `VehicleDrive` movement is unsupported in this teleport queue. Need one commit transaction for controller position, its occupants and gameplay seat/physics state, with proper collision/NavMesh destination validation.
3. No cross-region long-distance NPC pathfinding/streamed outdoor navmesh, no actual cold population progression, disk-backed region entity checkpoint/journal, evictable ECS generations or memory proof.
4. The shared W1 ledger and region loop still scan all tracked entities/tombstones each tick. Not large-city scale. Future stage should implement indexed event-driven admission and persistent disk region records.
5. `requestRegionMove` is an engine gameplay API for trusted callers, **not** permission validation for arbitrary network clients. Host game modules must validate the command/user/player/action before staging.
6. Bad memory-allocation exceptions in multiple ECS pools are not guaranteed exception-atomic; normal prevalidation failures are intended to leave state unchanged, but a generic ECS structural transaction is still missing.
7. Branch was 40 commits behind current `main` when W1.9 started. Do not blindly overwrite active main; rebase and resolve conflicting concurrent work before merged-tree testing.

## Commit history

- `e2faf2989bf6b0d96d47fd2873d1096d04977795` — persistent movement request schema
- `76364688782c2f6d4f8839e68ea03e681e6d6556` — ActorHost request/apply API
- `78c26792c2f1ceb8f3ae8082a2f890d33a247d65` — opt-in resource and early fixed-tick system
- `3b87284df2bae6ba5e8fa90535cb12be6dce35ff` — move staging/commit source
- `915ee0d3576458319c1e4f9b7d0c78ecbf998297` — const lookup fix
- `d3f57c2c647f43dddeffdde2d443d54b3b8bda44` — destination in readiness volumes
- `227ba43516b981e92346b5422f85a5bf208cbf83` — actor build target
- `6ffacde2eb36fcc31994a7f0cdcc3e66d46e5871` — first real admission CTest fixture
- `58839234f436c8cffc8327c42a8676f1e4b459b3` — CTest registration
- `d1e87d822811a80804bb409ae264000fa7c61903` — zero packed Entity ID fix
- `e98e127f6d9ee8a0e20d0e9657aa4e2460703b39` — admission cache member
- `5e578ae43ef469679219c9a4886c6a3dadba43a9` — readyToStep admission snapshot
- `face9b689f788e85712ba7bcc4d9bad9b0ac20a5` — prevent mid-tick bypass
- `306c0740d468bac9bca89574525e94298666289d` — late request test
- `d1e8815f55dc6ae15ccd1ebad332a744d58b3f1e` — native save queue validation
- `01cb1efac25b668a2d59c2bd282a8f5bc934189a` — API documentation
- `7cbf7ed4de98039c37a30954c7f2cf897faa5039` — queued-save roundtrip fixture

## Next agent steps — W1.10

1. **Before more features**, fetch current `main` and staged branch, reconcile differences, compile all W1 CTests in the required writable `/Volumes/KINGSTON/AIVERSE-builds/...` macOS workspace. Run the new `actor_region_admission_test` and all previous W1 tests, then `./run-tests.sh --quick`. Record exact compile/log failures and SHA. The tests are written, not yet known to pass.
2. Fix any differences in `ActorHost::readyToStep`, async streaming failure retry, or tiered collision/nav coverage exposed by these real CTests.
3. Route authoritative physics/teleport/vehicle controller movements through the same admission boundary (including typed vehicles) and prove no commands/RNG/tick change on denied destination.
4. Replace full-scan region reconciliation with per-entity ECS changes, stable region index and bounded persistence journaling.
5. Add actual world-space hot/cold NPC/car component ownership only after gameplay systems are dormant-aware and cross-reference-safe. Back it with disk region checkpoints and recovery.
6. Write a new discrete handoff with results, screenshots if actually captured, and decision about PR acceptance. No main merge until tested.

No release-ready W1 or production seamless world claim at this stage.
