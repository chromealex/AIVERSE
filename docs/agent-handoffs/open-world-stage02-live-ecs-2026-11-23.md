# Open World Framework — W1 stage 02: live ECS identity bridge
Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Main source baseline: `aec89ef435aba457ebeec24ef196f62a2fbb611d`
PR: https://github.com/chromealex/AIVERSE/pull/116 (**Draft**)
Area: **W1 / OW-01** (partial, NOT production ready)
Previous handoff: `docs/agent-handoffs/open-world-stage01-ledger-2026-11-23.md`

## Stage 02 implementation commits

1. `1701d1302d1eaa8a8d4fd39dc226444a81958fff` — `engine/world/RegionEcsBridge.h`
2. `78ebd5687df76b47fe1fd6a43e07e9e3df7121b4` — `tests/world/region_ecs_bridge_test.cpp`
3. `534d8ddce2122a96a4b0a44f2cddd5cef0d25299` — fixes COW snapshot mutation in previous `region_entity_ledger_test.cpp`.
4. `5edb664cbf52922c3cff57d9f9da77d3b29b50af` — CMake/CTest `region_ecs_bridge_test`.
5. `9d44eb20067342b386509d64e459855f0f531d07` — API and responsibility docs in `docs/region-entity-ledger.md`.

This stage is a source implementation, not a verified release.

## Architecture / APIs

`RegionStableIdentity {RegionEntityId id;}` — a POD ECS component with Archive ADL serializer. Register during `Simulation::setup` with `registerRegionStableIdentity(sim)`. `registerRegionEntityLedger(sim)` independently registers the persistent authority resource. Both opt-in declarations are native ECS v10 schemas: `aiverse.world.region_stable_identity` and `aiverse.world.region_entity_ledger`.

`RegionEcsBridge` static API:
- `bindNew(World&,RegionEntityLedger&,Entity,id,Cell,optional error)`: register a new stable id against an already-live ECS entity with `Transform`. Validates uniqueness and dead IDs. On ledger refusal, removes the tag installed in the preliminary step.
- `bindExisting(World&,const RegionEntityLedger&,Entity,id,expected Cell,...)`: attach newly materialized live actor to a non-tombstoned durable ledger record without duplicating its ownership.
- `transferGroup(const World&,RegionEntityLedger&,span<const Entity>,expected,destination,...)`: validate group live state and stable tags, then atomically move ownership as one ledger transaction. The host must group driver + passengers + cargo explicitly, and prevent destination physics entry until ready.
- `retire(World&,RegionEntityLedger&,Entity,expected,...)`: tombstone + destroy ECS entity at a safe structural mutation point, never inside pool iteration.
- `rebuild(const World&,const RegionEntityLedger&,map<stableId,Entity>&,...)`: reconstruct a **derived** index, reject duplicate/stale/dead live tags, but tolerate nonmaterialized ledger records. Must run after rollback/deserialize and ECS structural edits, not cached across them.
- `crossings(const World&,const RegionEntityLedger&,const SpatialWorld&,vector<Crossing>&,...)`: read-only detection using authoritative `Transform.pos` and fixed-point `SpatialWorld.cell`; sorted by stable ID. It does **not** mutate actor position/ownership or depend on renderer observers.

The existing `ChunkStreamer` remains presentation-only. New engine code has no project-specific, gameplay-genre, or GTA2 imports.

## Tests / evidence

Added `tests/world/region_ecs_bridge_test.cpp` (CTest `region_ecs_bridge_test` / labels World,ECS). The written test:
- creates an actual `Simulation`, `World`, native ECS persistent `RegionEntityLedger` and `RegionStableIdentity` component schemas;
- registers a synthetic test-owned Transform archive codec, never overwriting existing production codecs;
- binds two actors, checks duplicate refusal and stable ID lookup rebuild;
- moves vehicle/passenger Transform positions across a spatial region boundary, detects changes, rejects stale source and performs one grouped ledger transfer;
- simulates manual entity reattachment with caller-held Transform, checks retirement and tombstone refusal;
- crosses 100 consecutive 32-unit regions with two real ECS Transform components;
- serializes actual Simulation, restores in an independent Simulation, tests COW snapshot rollback and peer checksums.

Fixed previous stage `region_entity_ledger_test`: after `saveState(snapshot)`, a mutable ECS resource reference must be reacquired using `World::resource<RegionEntityLedger>()` before a mutation, otherwise COW shared resource snapshots may be affected.

**Actually run this stage:** GitHub connected source inspection of CMake + header + test source, and two Linux clang++ C++20 syntax smoke tests for implicit vector→`std::span<const uint64_t>` and array→`std::span<const Entity>` conversion (both passed). These small compiler checks only confirm the chosen standard-library conversion APIs. The new AIVERSE test target was **NOT BUILT OR RUN** and no integration/p95 performance data was obtained; do not report PASS for the new CTest.

**Unavailable here:** user's macOS + `/Volumes/KINGSTON` build volume. No fallback build directory is allowed by the user's AGENTS.md instructions. `./run-tests.sh --quick` on the merged tree has not been performed.

## Limitations and remaining debt

- No authoritative component payload parking/reification yet. `bindExisting` is a handshake only; a caller must load *all* component state itself.
- The stable identity attached to the live Entity is a registered opt-in component, but no production `GameCore` host or movement system wires it into automatic region transactions.
- No persisted spatial derived index (intentionally); `rebuild` and `crossings` scan all live tagged entities; optimize carefully only after correctness is established.
- No collision/NavMesh readiness barrier, no async epoch/fencing, no world-scale disk journal or cold population simulation.
- `transferGroup` trusts the host to supply *all* dependent occupants and to validate any group/seat relationship. It does not discover cargo links.
- Do not mark W1 `core`; world roadmap stays `in-progress` until actual Mac build + complete integration acceptance.
- No screenshot/site devlog because this is an internal API without verified visual runtime.

## Next agent entry point — stage 03

1. **Before more runtime features**, compile both W1 targets on a current Mac merged tree using `/Volumes/KINGSTON/AIVERSE-builds/<branch>`, run CTests `region_entity_ledger_test` and `region_ecs_bridge_test`, then `./run-tests.sh --quick`. Record actual SHA, results, count and failures. Fix any compiler/API errors first.
2. Integrate a simple real `GameCore` ECS actor and `engine/vehicles` vehicle host behind an opt-in rule block, reusing the shared `RegionEcsBridge`. Never add a genre-specific simulation path.
3. Define a versioned persistent dynamic payload store for region-parked ECS entities (including entity generations/references, transformation, vehicle occupant state and cross-region links). Need atomic hot↔cold transitions without loss or entity duplication. A `World::destroy` on visual unload alone is forbidden.
4. Add `readyToStep` collision/navmesh residency fence and a two-region gate fixture. Load failures/teleport must block a step without advancing RNG, tick or commands.
5. Test client-region load budget differences, two observers, real 100 crossing movement, destruction during unload, restart save/checksum and COW rollback.
6. Check GTA2 generic policy reuse only when the shared engine capability is accepted; GTA2 retains its project specific content and old private-engine patches are not applicable to current main.

No release notes yet: W1 is partial. There are no new screenshots or benchmarks to publish. The handoff has been created on the feature branch and may be continued without asking the user to restate the plan.
