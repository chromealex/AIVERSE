# Open World Framework — W1 stage 03: durable cold ECS payloads
Date: 2026-11-23
Repo: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Main baseline reviewed: `aec89ef435aba457ebeec24ef196f62a2fbb611d`
Area: W1 / OW-01. Capability status stays **partial** and world roadmap stays **in-progress**.
Previous stage: `docs/agent-handoffs/open-world-stage02-live-ecs-2026-11-23.md`

## What changed

This stage adds a generic, opt-in hot/cold **ECS component parking** mechanism while retaining the actual ECS entity, its generation, `Transform`, stable ID component and authoritative region-ledger ownership. The feature is not tied to a project/genre or presentation camera. It does not create a second ECS simulation.

Files:
- `engine/world/RegionColdStore.h` (new): `RegionColdStore` durable component blobs, `RegionDormancyController`, Archive codec, persistent ECS registrations and load validator.
- `engine/world/RegionEcsBridge.h`: `RegionDormant` persistent marker. `RegionEcsBridge::transferGroup` and `retire` reject dormant entities instead of moving/retiring only a fragment of state.
- `tests/world/region_cold_store_test.cpp` (new): source fixture for real `Simulation`, `World`, actor+passenger, `Brain` and `Engine` components; archive and ECS restore.
- `CMakeLists.txt`: CTest `region_cold_store_test`, labels World,ECS, target `aiverse_region_cold_store_test`.
- `docs/region-entity-ledger.md`: stage-03 API and limitations.
- This separate handoff document.

Stage commits:
- `9d531a0e6fa9e8715ee9db978461d5e634cfcc11`: cold marker and transfer/retirement guards.
- `7d16402cd75d09dc6caa46772c2dcbd5d5a1b254`: cold-store initial header.
- `5a2f9965f0eabc10553d3f4c3c66036427621662`: decode stable ID by reference.
- `fd3b0e3387d554cde13e01c6e7fa2542621b55f0`: cold-store test.
- `26c1c5e3ae88ad5fbf618d533cb1982fbdeb51c1`: CMake test registration.
- `ef4c6c9476a0dbc1d61a3c5834c28d88267a6113`: bounded schema and load consistency validation.
- `c75d68f3ac94a0cb3c60d3c647972ff7f4981c5d`: tests for partial-group refusal and invalid save.
- `bd79681c5479464a2936094c3d2c5a1676ad7faf`: duplicate dormant ID validator check.
- `e95cd296df54d71cd701bda43bc1a3c14d20eb60`: full-region hot/cold idempotent transition.
- `51fc0b8223cf361da81a85d68fdc1b2f9a956df8`: region transition + two-region save/restore tests.
- `e140570ed6a626280031ecdbbaf73cc9bafcef93`: documentation.
- `bca5a717300bac5cfaeaef3857124ab760cc3be5`: explicit policy for Entity handles inside parked game codecs.

## Architecture and public API

- `RegionColdStore`: `find(id)`, `size()`, versioned resource archive. Stores `RegionColdRecord { Cell owner; ownerRevision; map<stableComponentName, RegionColdBlob> }`. No arbitrary C++ object byte casts.
- `registerRegionColdState(sim)`: registers persistent ECS resource `aiverse.world.region_cold_store`, persistent `RegionDormant` component, and a persistent-load cross-schema validator. Call **during setup** together with `registerRegionEntityLedger` and `registerRegionStableIdentity`, then `setResource<RegionColdStore>`.
- `RegionDormancyController::registerComponent<T>(stableName,version)`: explicit parking-codec registry; `T` requires portable ADL `io(Archive&,T&)`, default and copy constructors; forbidden for `Transform`, `RegionStableIdentity`, `RegionDormant`.
- `parkGroup(world,ledger,cold,entities,owner)`: validates and pre-encodes every group member, budgets entire candidate record store, then removes registered components and tags `RegionDormant`.
- `wakeGroup(...)`: validates all owner revisions and component codecs, decodes the entire group, then reinstalls components and clears dormant markers and cold blobs.
- `retireDormant(...)`: deletes parked payload and destroys ECS entity **only** after recording an authoritative tombstone.
- `setRegionActive(...,region,bool active)`: deterministic, idempotent whole-region transition using ledger's `activeIn`. Requires every tracked entity in that region to remain materialized, and rejects mixed hot/cold or missing/stale records. Caller selects state based on **simulation**; view/ChunkStreamer cannot silently control authority.
- Limits: 65,536 parked IDs, 64 parkable components per entity, 1 MiB/individual blob, 16 MiB/resource including metadata; map iteration is deterministic.
- Decoding is transactional; duplicate names/IDs, invalid versions/over-budget data and truncation fail closed. Load validator ensures tag-to-record correspondence and matching ledger owner/revision, including duplicate dormant IDs.

This parking mechanism is deliberately *not* the dynamic region disk pager. ECS handles stay valid (the entity remains allocated), only explicitly registered components leave their live ECS pools. It is a correctness slice for further streaming integration. Users must gate all systems touching a parked component with `RegionDormant`, because `Transform` remains present.

### Exception and determinism limitations

- Group validation and codec-decoding failures do not publish partial changes.
- Bad allocations or arbitrary exceptions during actual ECS structural mutation can still leave partial changes; no global ECS transaction/undo API is implemented. Handle this as a host failure and do not claim fully transactional operational guarantees.
- Parking and waking can change ECS dense-pool ordering relative to other live entities. Resulting save/checksum is deterministic only when the same authoritative region transitions run in the same fixed order. A generic order-preserving ECS pool compaction mechanism would require separate work.
- Persisted component codecs may include Entity references; because Entity indices/generations are retained, the host must still validate cross-region references and migrations.
- The store copies its current map for budget preflight (cost proportional to stored cold data); optimize with a proven bounded-region append/journal or transactional API after correctness.
- A real large world needs offscreen time progression, collision/NavMesh gating, disk-backed region save, stable reference ownership and host integration before W1 can be accepted.

## Tests — scope and actual evidence

CTest name: `region_cold_store_test`, source `tests/world/region_cold_store_test.cpp`. Written cases:
1. Group with invalid/unbound entity rejects before removing any live components.
2. Valid active car+passenger parks two real ECS components per entity while preserving Entity handles, Transform and stable ID.
3. Repeat park and repeat wake are rejected/no-op as appropriate; cold records cannot be moved/retired through the hot-only bridge.
4. Archive bytes round-trip and truncated-buffer refusal leave old store unchanged.
5. Wrong codec version refuses wake without half-restoring.
6. Fresh Simulation restore of parked data restores correct components; full park/wake with two identities reproduces baseline checksum for this fixture.
7. COW snapshot rollback; dormant retirement; tombstone survival in fresh save.
8. Region-wide deactivation/reactivation is idempotent; two adjacent regions can park independently and restore after save.
9. Inconsistent but checksum-valid saves (missing `RegionDormant` tag vs parked payload) fail persistent cross-schema validation and do not overwrite destination simulation.

**Actually executed during this stage:** source inspection through connected GitHub, checks for registration/presence of header/test/CMake/docs, and inspection of `PersistentEcsRegistry::serialize` verifying that load validators are applied to a staged World before world replacement. **The new C++ code has not been built or run** against the current AIVERSE root CMake, and no CTest PASS can be claimed. No game capture or platform benchmark was performed.

**Reason:** this runtime does not have the owner's macOS machine or required writable `/Volumes/KINGSTON` build volume; owner policy forbids silently building elsewhere. No `./run-tests.sh --quick` pass is claimed.

## Next agent steps — W1.4

1. Refresh branch vs main and other W1 work; keep WIP limit. On Mac with KINGSTON, build three W1 test targets:
   `aiverse_region_entity_ledger_test`, `aiverse_region_ecs_bridge_test`, `aiverse_region_cold_store_test`; run CTest regex `^(region_entity_ledger_test|region_ecs_bridge_test|region_cold_store_test)$`. Fix compiler/API and failing assertions first.
2. Run `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug` **on merged tree**; record commit SHA, pass count, failure list and ownership.
3. Add real `GameCore` capability-enabled host integration and a no-genre sample that moves a car with occupants. Import *no* GTA2 game assets or logic into the engine.
4. Define the **authoritative** hot/cold scheduling and offscreen progression policy, unrelated to renderer camera/asset chunk residency. Plan backpressure, memory/disk budget and stable references.
5. Add collision/NavMesh readiness fence using `GameModule::readyToStep`, with blocked destination and failure/teleport tests (tick, RNG and commands unchanged on refusal).
6. Design per-region atomic disk checkpoint/journal with schema migrations and crash recovery, and update the resource read/write API without silently changing other games' snapshots.
7. Only claim W1 `core` after integration, 100 *actual* region crossings, restart checksum parity, p95 performance and merged Mac CTest pass. Capture engine screenshot and web demo only for proven visible stages.

No release-ready runtime or website publication from this unverified internal stage. PR #116 stays Draft. Update per-stage documentation again before moving to W1.4.
