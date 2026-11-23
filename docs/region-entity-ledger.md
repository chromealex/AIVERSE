# Persistent region entity ownership (W1, initial policy slice)

The `engine/world/RegionEntityLedger.h` API records **authoritative** stable identities and region ownership of dynamic entities. It is deliberately independent of `ChunkStreamer` and `AsyncChunkStream`: the latter select **presentation resources** (models, collision resources, geometry) according to observers and budgets, and are not deterministic simulation state.

**Status:** first isolated policy and ECS v10 persistence test added. **Not yet complete seamless ECS region streaming.** No generic host currently populates this ledger automatically, parks ECS entity component pools on eviction, reifies offscreen simulation, or refreshes collision/navmesh from a transferred entity. Treat W1 as partial.

## API

```cpp
#include "engine/world/RegionEntityLedger.h"

using namespace aiverse::world;

// During GameModule::setup (opt-in only; changes the game's save schema to v10):
registerRegionEntityLedger(sim);
sim.world().setResource<RegionEntityLedger>(RegionEntityLedger{});

// During deterministic Simulation tick handlers:
auto* ledger = sim.world().resource<RegionEntityLedger>();
ledger->spawn(1001, Cell{0,0,0});  // a stable, nonzero game identity
ledger->spawn(1002, Cell{0,0,0});  // passenger identity

// Car and occupants move together, atomically; no partial transfer on error.
std::array<RegionEntityId, 2> occupants{1001,1002};
ledger->transfer(occupants, Cell{0,0,0}, Cell{1,0,0});

// An actual destruction is a tombstone; it does not free the ID for reuse:
ledger->despawn(1001, Cell{1,0,0});

// Read-only queries: activeIn(cell), find(id), since(revision),
// lastRevision() and size().
```

Include `<array>` when using `std::array` in the caller. The host assigns stable IDs and maintains the mapping to live ECS Entity index+generation; the persistent ID is not a temporary ECS index. Every entity must be spawned once and must not be reused after a tombstone. Updates, transfers and despawns must happen in a deterministic simulation system, **never** from render/asset callbacks, LOD observers or asynchronous threads.

`transfer(ids, expectedCell, destinationCell)` validates every input before updating any record. IDs are sorted internally, so permutation of the same group produces the same serialized bytes and hash. Wrong expected owners, duplicates, tombstones or missing IDs fail without mutation. Moving to the same cell is a no-op. Every successful change gets a monotonically increasing revision.

The ledger's `Archive` codec is versioned, bounded to 1M IDs, sorted, checks duplicate IDs/revisions, rejects invalid live flags, and decodes transactionally: a corrupt/truncated buffer does not replace an existing resource. `registerRegionEntityLedger` delegates normal save/checksum/rollback to `Simulation::registerPersistentResource` (native ECS v10).

## Ownership vs residency

- **Ledger:** saved, authoritative; keeps **all** IDs including tombstones and records whose owner is far away. Never cull it because a rendering observer moved.
- **Presentation residency:** `ChunkStreamer` and `AsyncChunkStream` can unload meshes, effects, transient collision data and other views. They must never independently alter the ledger.
- **ECS entity activation:** **not implemented in this slice**. The future host needs a transactional region store for component payloads, authority fences, meaningful cold/offscreen updates and exact restore. Do not call `World::destroy` just because a visual chunk was evicted.
- **Collisions/navmesh:** the future host needs a barrier across boundaries and atomic updates for a car with passengers. If destination physics is unready, block admission or retry without advancing the simulation tick.
- **Observability:** `since(revision)` reports the last change of each ID, including moved-away IDs and tombstones. This is a change projection, not a complete replay/event journal.

## Test coverage

`tests/world/region_entity_ledger_test.cpp`, CTest `region_entity_ledger_test`:
- Duplicate/invalid spawn, permutation-invariant atomic group transfers, wrong expected owner and duplicate refusal, no-op transfers.
- Tombstones and no resurrection, revision deltas, archive roundtrip, corrupted/truncated archive transactional refusal.
- 100 sequential cross-region transfers for a vehicle/occupant group.
- Registered native ECS resource checksum, full save/restore, COW snapshot rollback, two simulated peers and fresh-instance restore.

Run via `ctest -R '^region_entity_ledger_test$'` after building the new target on a supported environment. The acceptance bar for the larger W1 capability still includes real streamed worlds, active ECS entities, persistence on eviction, collision/navmesh coordination, sustained p95 timings, and platform testing.

## Architecture and limitations

The initial implementation deliberately uses a sorted `std::map` of up to one million stable IDs. Mutation is logarithmic; `activeIn(cell)` and `since(revision)` scan the ledger, so callers must not poll all cells every tick. Add a derived nonauthoritative spatial index after correctness tests, or a versioned regional store once cold persistence is implemented.

Do not move a specific game's taxi/fleet/mission code into this layer. The same ledger must serve players, NPCs, vehicles, cargo, animals, machinery and buildings in any project that opts in.

Stage note: `docs/agent-handoffs/open-world-stage01-ledger-2026-11-23.md`.
