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


## Live ECS bridge — stage 02

Include `engine/world/RegionEcsBridge.h` in a gameplay system that owns
actual live `Transform` components. Register two schemas **during setup**:

```cpp
registerRegionEntityLedger(sim);  // resource
registerRegionStableIdentity(sim); // component
sim.world().setResource<RegionEntityLedger>(RegionEntityLedger{});
```

When spawning an existing ECS actor in deterministic gameplay, call:

```cpp
auto& world=sim.world();
auto* ledger=world.resource<RegionEntityLedger>();
const Entity car=world.create();
world.add<Transform>(car,Transform{});
RegionEcsBridge::bindNew(world,*ledger,car,1001,Cell{0,0,0});
```

The vehicle system or AI movement already owns `Transform`. To transfer an
entity with all affected occupants, pass the **whole group**:

```cpp
std::array<Entity,2> group{car,passenger};
RegionEcsBridge::transferGroup(world,*ledger,group,Cell{0,0,0},Cell{1,0,0});
```

`passenger` must already exist and be registered, and the call must only be
made after gameplay has validated its parent/seat ownership. The bridge does
not infer attachments, move the player, copy any component, or create a
passenger itself. All group members are validated before the ledger transfer;
a stale or missing binding fails without moving any owner.

`RegionEcsBridge::crossings(world,*ledger,spatial,pending)` reports
region-boundary candidates from authoritative fixed-point Transform values.
**It is read-only.** Cameras, view-origin rebasing, LOD, streaming budgets
and asynchronous loaders cannot trigger authoritative changes through it.
`RegionEcsBridge::rebuild(world,*ledger,idToEntity)` reconstructs a derived
lookup table sorted by stable ID. Rebuild it after loading a save, rollback
or changes in ECS pool membership. Missing live bindings are allowed because
a region can be cold; an invalid, duplicate or tombstoned live binding is
rejected without altering the previous lookup table.

`RegionEcsBridge::bindExisting` permits attaching a newly materialized
live entity to an existing owner record. It does **not** load or preserve that
entity's arbitrary ECS component state. The caller must deserialize the
complete component payload first and verify its region. `retire` records a
durable tombstone and destroys the ECS entity; call only at a safe structural
mutation point, never from inside an ECS pool iteration.

### Stage 02 tests

`tests/world/region_ecs_bridge_test.cpp`, CTest `region_ecs_bridge_test`
covers duplicate refusal, live index rebuild, fixed-point boundary
detection, group transfer, manual reattachment, retirement, 100 successive
live Transform crossings, portable native ECS save and a COW rollback.
The independent test fixture supplies an explicit Transform archive codec.
A production module must use its actual existing Transform persistence
contract rather than registering a second conflicting codec.

### Not yet delivered

No production host calls these APIs automatically. Missing: authoritative
ECS state parking/reification, collision/navmesh barrier, active/cold tier
scheduler, externally journalled region storage, automatic group discovery,
runtime instrumentation and Mac/KINGSTON full-suite acceptance. W1 remains
`partial`. The bridge is an opt-in reusable layer, not a complete seamless
open-world feature.


## Stage 03 — persisted hot/cold ECS component parking

`engine/world/RegionColdStore.h` now supplies a **manually invoked, opt-in**
simulation-tick parking mechanism. This is not async loading of arbitrary
component graphs and does not intercept ordinary ECS systems.

### Setup (GameModule::setup)

```cpp
#include "engine/world/RegionColdStore.h"

using namespace aiverse::world;
registerRegionEntityLedger(sim);
registerRegionStableIdentity(sim);
registerRegionColdState(sim); // includes a cross-schema save validator
sim.world().setResource<RegionEntityLedger>(RegionEntityLedger{});
sim.world().setResource<RegionColdStore>(RegionColdStore{});

RegionDormancyController controller;
controller.registerComponent<MyAiBrain>("my.ai.brain",1);
controller.registerComponent<MyVehicleState>("my.vehicle.state",1);
```

`MyAiBrain` and `MyVehicleState` are **illustrative**. A registered type must
have a complete portable `io(Archive&,T&)` codec. It must be default- and
copy-constructible, and every system using the type must explicitly skip
entities with `RegionDormant`. The host must determine when a region
becomes hot or cold from authoritative, deterministic gameplay state, never
directly from view/camera observer or `ChunkStreamer` load budgets.

### Parking and reactivation

```cpp
auto& world=sim.world();
auto* ledger=world.resource<RegionEntityLedger>();
auto* cold=world.resource<RegionColdStore>();

// Run at an ECS structural-mutation barrier on a game tick:
controller.setRegionActive(world,*ledger,*cold,Cell{0,0,0},false);
controller.setRegionActive(world,*ledger,*cold,Cell{0,0,0},true);

// Explicit group alternative (e.g. car + passengers):
std::array<Entity,2> group{car,passenger};
controller.parkGroup(world,*ledger,*cold,group,Cell{0,0,0});
controller.wakeGroup(world,*ledger,*cold,group,Cell{0,0,0});
```

`parkGroup` validates every member, marshals configured present components
to versioned blobs, checks the entire resource budget, then removes those
components and sets `RegionDormant`. `wakeGroup` validates and fully
decodes the whole group before restoring components and clearing cold
records. This avoids partially changing a group for normal validation,
schema/version or corrupt-payload failures. It is not an absolute guarantee
of rollback on C++ allocation exceptions during ECS structural changes;
those are an explicit host error boundary.

Entity identity, `Transform`, `RegionStableIdentity`, and the permanent
`RegionEntityLedger` record stay alive in both states. Consequently,
a valid ECS handle to that entity is retained and it cannot be respawned
from a stale asset-streaming callback. A dormant actor cannot transfer
ownership through `RegionEcsBridge::transferGroup`; it must wake first.
For actual gameplay destruction, use
`RegionDormancyController::retireDormant` so the cold payload is removed
together with the permanent ledger tombstone.

`setRegionActive` walks **all** ledger-owned entities in a region and
requires each to retain a live ECS identity/Transform. It fails closed for
unmaterialized IDs, mixed hot/cold state, corrupt records, or entities
without a supported parked component. Idempotent transitions perform no
writes. This is a first whole-region transition policy and intentionally
does not implement offscreen simulation, component-level cold scheduling,
cross-region reference fixups, render residency, or entity deallocation.

### Persistence / correctness rules

The cold store is a separately registered ECS v10 persistent resource
(`aiverse.world.region_cold_store`), and `RegionDormant` is a persistent
component. Its Archive schema is bounded (65,536 entities, up to 64
components/entity, 1 MiB/blob, 16 MiB total including metadata).
Decoding is transactional and rejects invalid IDs, duplicates,
zero versions, truncated data and oversized payloads.

`registerRegionColdState` installs a persistent-ECS load validator
ensuring each dormant tag matches exactly one parked record and live
ledger owner/revision. Inconsistent but otherwise checksum-valid saves
are rejected. The host must still ensure the set of registered component
codecs agrees with its gameplay state and loaded schema: the global
validator cannot inspect arbitrary game-defined component types.

Every load and COW rollback uses standard ECS snapshot and
`Simulation::serialize`/ `deserialize`; caller-held pointers must be
reacquired with `world.resource<T>()` after a snapshot. The region state
and component payloads are authoritative and must **never** change because
a camera moves or a visual chunk is evicted.

### Stage 03 tests / current limits

`tests/world/region_cold_store_test.cpp`, CTest
`region_cold_store_test` (labels `World;ECS`) covers grouped real ECS
component parking and unpark, roundtrip archive and fresh-instance
save/restore, corruption and codec version refusal, no-op hot/cold
transitions, neighboring-region independent activation, tombstones,
rollback and consistent failure on partial groups.

**As of stage 03 this test has been written and registered but not
compiled/run on a Mac.** Full W1 acceptance still needs a
production-game integration, persistent disk-backed regional payload
ownership, NavMesh/collision barriers, event/timer policy for cold
regions, performance checks, and a green Mac/KINGSTON merged-tree suite.
Keep `W1` partial and the PR draft.


## Stage 04 — authoritative Simulation lifecycle in real GameCore

`engine/world/RegionLifecycleRuntime.h` exposes a shared, opt-in
**gameplay-facing** activation controller. Unlike `ChunkStreamer`, it is
installed during `Simulation::setup` and consumes explicit validated
gameplay requests on simulation ticks. The user-facing game enables it
through a native C++ module configured in the existing `nativeModules`
data block (or another shared game host's setup); there is no new genre
host, renderer callback or camera-derived activation policy.

### Native gameplay integration

```cpp
// In NativeModule::setup, during the main Simulation setup:
RegionDormancyController codecs;
codecs.registerComponent<MyBehaviour>("game.behaviour",1);
RegionLifecycleRuntime::install(sim,std::move(codecs),2);
// Register MyBehaviour with the normal persistent ECS registry as well.
// Select real ECS actors and attach RegionStableIdentity with
// RegionEcsBridge::bindNew(...) after creating their Transform/components.

// In NativeModule::command, *after* validating the player and action:
RegionLifecycleRuntime::request(sim,Cell{x,0,z},false); // park region
RegionLifecycleRuntime::request(sim,Cell{x,0,z},true);  // wake region
```

`RegionLifecycleRuntime::install` registers the region ledger, stable
identity, dormant marker, cold store, activation queue, load validators,
and a named `world.regionLifecycle` system. The caller owns the
parkable-component codecs and must register their independent persistent
ECS schemas if they can remain hot. The controller is immutable after
installation and is captured safely by the system; its mutable
authoritative state is entirely in ECS resources.

`RegionActivationQueue` is a versioned persistent resource
(`aiverse.world.region_activation_queue`), with at most 1024 pending
region targets, sorted by `Cell`. Each value is 0 (cold) or 1 (hot).
A `request` for the same region replaces that region's pending
target. `RegionLifecycleRuntime::step` processes at most
`maxTransitionsPerTick` (1–16) on each authoritative simulation tick.
A successful region transition consumes the pending entry and increments
its persisted applied counter. If the first pending region cannot
transition (for instance a ledger entity has no live binding), processing
stops and the request remains queued. No later region changes in that
tick. A new validated command may replace the pending target.

**The region activity request is an authoritative gameplay decision.**
AI, missions or verified server commands may issue it; render observers,
camera distance and asynchronous loading should only influence visual
residency. This stage deliberately does not automatically decide which
region should be cold. A production scheduler must supply simulation-
deterministic interest, occupancy, timing and safe collision/NavMesh
readiness rules.

### Real GameCore acceptance fixture

`tests/world/region_gamecore_integration_test.cpp` uses an actual
`game::GameCore` and C++ `game::NativeModule` through the existing
`nativeModules` registration path. It attaches a stable region identity
to a real map-spawned `game::Unit` with `Transform`, parks a
fixture-only `RegionWork` ECS component and preserves the real
gameplay entity, validates the native command's player, then uses
the engine's actual save, load and COW rollback paths.

The test demonstrates the shared integration hook, **not** the automatic
parking of GameCore's `Unit`, `Mobile`, `VehicleDrive` or
`VehicleOccupant`. Those production components must not be removed
until their owning gameplay systems explicitly skip `RegionDormant`
and preserve their invariants. It also does not prove that a city remains
safe when collision/navmesh chunks are unready.

CTest target: `aiverse_region_gamecore_integration_test`, name
`region_gamecore_integration_test`, links `aiverse_game`, uses the
normal external test template package. It is source-registered, but
actual Mac/KINGSTON compilation and runtime checks have **not** been
performed yet. The PR remains Draft and W1 remains partial.
