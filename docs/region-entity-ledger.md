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


## Stage 06 — vehicle/occupant region grouping (shared ECS)

`engine/world/VehicleRegionTransfer.h` reads the existing shared
`vehicles::VehicleDrive` and `vehicles::VehicleOccupant` components.
It is a pure validation/transfer adapter, **not** another car physics
or seating implementation.

```cpp
using namespace aiverse::world;

// The world-space ActorHost maps seat actor IDs to real CharacterIndex
// entities. Other hosts provide their own stable actor-ID mapping.
SeatEntityResolver seatToEntity = [&](int64_t slot) -> Entity {
    return slotEntity(slot);
};
VehicleRegionGroup group;
if(VehicleRegionTransfer::gather(world,car,seatToEntity,group,&error)) {
    // Before transfer, validate destination collision + navigation residency
    // using the actual owning host, then:
    VehicleRegionTransfer::transfer(
        world,*ledger,car,seatToEntity,source,destination,&error);
}
```

The group contains the vehicle entity, its `VehicleDrive::driver`,
all `seats.occupants` resolved through the host callback, and all
`VehicleOccupant` ECS backlinks to the same vehicle. The result is
sorted by ECS entity handle; duplicate/invalid seat actors, stale
driver/entity handles and contradictory backlinks fail. The
transfer path delegates to `RegionEcsBridge::transferGroup`, which
validates every member's stable ID, `Transform`, non-dormant status
and original owner **before any ledger owner is changed**.

The world-space host now exposes
`actor::ActorHost::vehicleRegionGroup(sim,vehicle,out,error)` as a
**read-only adapter** mapping integer seat actors to `CharacterIndex`
entities. It does not install `RegionStableIdentity`, move any actor,
or park `VehicleDrive` or `CharacterBody`.

**Known gap:** world-space `ActorHost` characters store their position
in `actor::CharacterBody`, and do not automatically have
`aiverse::Transform`. The current W1 `RegionEcsBridge` requires a
`Transform` on every group member. Consequently, the new adapter
can correctly discover a real occupied vehicle, but its region
ownership transfer must fail until a supported, authoritative
world-space position/identity binding is implemented and validated.
Do not add a cosmetic `Transform` or a parallel position model
just to bypass this condition.

Tests:
- `tests/world/vehicle_region_group_test.cpp` (CTest
  `vehicle_region_group_test`) builds actual shared vehicle ECS
  components, validates occupants/backlinks and transfers a fully
  registered car+driver+passenger, including all-or-nothing refusals.
- `tests/world/actor_vehicle_region_group_test.cpp` (CTest
  `actor_vehicle_region_group_test`) constructs real world-space
  `GameCore`/`ActorHost`, resolves game-player seat slots and
  confirms an incomplete W1 registration cannot mutate ownership.

These tests are source-registered but **not compiled or executed on
Mac/KINGSTON yet**. W1 remains partial; the existing native game
world/vehicle simulation has not been switched over to the cold
region lifecycle. Next work: canonical shared positions, component
owner participation, collision/nav admission, and full deterministic
save/replay before production activation.


## Stage 07 — canonical position owner for real world-space actors

The generic W1 policy no longer requires a duplicate `Transform`
when the host already owns an authoritative position elsewhere.
`RegionPositionReader` is a read-only adapter
(`std::optional<Vec3>(const World&,Entity)`). The original
`Transform`-only overloads remain in `RegionEcsBridge`; the new
overloads for `bindNew`, `bindExisting`, `transferGroup`,
`rebuild` and `crossings` receive the host's reader explicitly.

```cpp
// For real world-space GameCore/ActorHost:
auto position=actor::ActorHost::regionPositionReader();
auto* ledger=sim.world().resource<world::RegionEntityLedger>();
world::RegionEcsBridge::crossings(
    sim.world(),*ledger,world::SpatialWorld(32),crossings,position);

// The host's existing typed vehicle seat resolver is used automatically:
actor::host(sim).transferVehicleRegion(
    sim,car,world::Cell{0,0,0},world::Cell{1,0,0},&error);
```

### Enabling it

Use the root rules block `"regionOwnership": {}` on a
**world-space GameCore map**. It registers the existing native ECS
`RegionEntityLedger` resource and `RegionStableIdentity` component
at `ActorHost::setup`, then binds all initial live characters and
vehicles using a deterministic stable ID (the packed entity handle
plus one). The location is read from the **existing**
`actor::CharacterBody.pos` or `actor::Vehicle.pos` component;
other entities may provide `Transform` as before.

This is opt-in: projects without `regionOwnership` do not register
any W1 persistence schemas, so their default save format and
checksums stay unchanged. The current stage uses fixed 32-unit
regions; configurable units/large-world cell ownership still need
explicit work.

`ActorHost::transferVehicleRegion` uses the shared
`VehicleRegionTransfer` for atomic car + occupant ownership
updates, but first requires existing `ActorHost::readyToStep`
collision/nav admission. On asynchronous maps it also requires
`CollisionStream::covers` for the car's destination bounding box.
It then checks that the vehicle's **canonical** position belongs
to the destination W1 region and that every group member already
belongs to the expected source region. No authority update occurs
on failure.

Vehicle physics/seat changes remain entirely in
`engine/vehicles` and `ActorHost`; no cloned position, actor
controller, or seat simulation has been added.

### Hot/cold compatibility

The shared `RegionDormancyController` also accepts
`setPositionReader` and passes this reader to
`registerRegionColdState` load validation through
`RegionLifecycleRuntime::install`. This supports safe, expressly
registered optional components on world-space actors without a
`Transform`. It does **not** automatically park
`CharacterBody`, `VehicleDrive`, vehicle health or combat state:
their systems would need explicit dormant handling, and their
persistence ownership must remain consistent.

A normal game using only `regionOwnership` registers ledger and
stable IDs, **not** the hot/cold runtime or cold-store resources.
Installing the latter in a production host still requires validated
parkable component codecs and a deterministic activation policy.

### Tests and limitations

Added `tests/world/actor_region_position_test.cpp` (CTest
`actor_region_position_test`). It uses an actual world-space
`GameCore` with `CharacterBody` and `Vehicle`, tests opt-in
native ECS IDs, real typed seat link discovery over a synthetic
`VehicleDrive`, read-only crossings, atomic transfer, optional
component park/wake, ECS v10 full save/restore, COW rollback and
legacy v9 format with no opt-in. The fixture's synthetic
`VehicleDrive` is removed before full game save since it is not
part of the map's ordinary vehicle configuration.

Tests are committed and CMake-registered but **not compiled or
executed** in the required Mac/KINGSTON checkout yet. W1 remains
partial. Still needed: real vehicle movement transitions from the
gameplay tick, dynamic respawns/destructions tracked in ledger,
source/destination load retry integration, actual hot/cold guards
for production character/car systems, offscreen simulation, disk
region journal and performance validation. Branch needs integrating
with the more recent `main` before acceptance.


## Stage 08 — automatic world-space actor region reconciliation

When a real world-space `GameCore` is configured with
`"regionOwnership": {}`, `ActorHost` now registers an
`actor.regionOwnership` ECS system **after its normal actor system**.
The system calls `ActorHost::synchronizeRegionOwnership(sim)` at the
end of each authoritative fixed tick; it also runs once at setup
after any race or bot initial placement. Projects without the
rules block do not install the system, resource or component schema.

The existing `CharacterBody.pos`, `Vehicle.pos`, and `Loot.pos`
are canonical and read through `RegionPositionReader`. They are
not copied into synthetic `Transform` components. The first
W1 ledger schema continues to use 32-unit region cells.

### Reconciliation policy

1. Read live characters, map vehicles and durable loot into one
   sorted ECS list. For a newly observed entity, assign a
   nonzero stable ID from `Entity::packed()+1`, including the
   generation counter, and create its ledger record. Reused ECS
   indices have different generations, so they get new durable
   IDs and cannot revive an old tombstone.
2. Resolve `vehicles::VehicleDrive` occupied seat groups using
   the existing shared `VehicleRegionTransfer`. The arcade
   `actor::Vehicle` path instead resolves its own
   `Vehicle.driver` player slot and `CharacterBody.vehicle`
   backlinks, without another vehicle simulation.
3. Relocate a complete car group to the car's canonical region
   in one ledger operation. Newly boarded passengers may
   arrive with different previous owners: the ledger
   `transferGroup` overload validates each expected source
   independently, then changes all necessary owners in stable
   ID order. An invalid/stale/dormant member causes the group
   to be rejected rather than partially changed. The ActorHost adapter
   also rejects an orphaned `CharacterBody.vehicle` backlink or duplicate
   `Vehicle.id` before attempting any ownership change.
4. Independently migrate on-foot actors and loot whose region
   changed. Those already handled as car occupants are skipped,
   preventing the same tick from moving one passenger twice.
5. Compare still-live ledger records to observed actor identities
   and create permanent tombstones for removed ECS entities.
   Fast, short-lived `Projectile` entities are deliberately
   excluded from this persistent ID ledger: retaining every
   spent bullet would exhaust the bounded ledger.

The host's existing `readyToStep` is still the collision/nav
gate before a simulation tick. For a car whose post-tick
destination somehow escapes that preloaded movement envelope,
the reconciliation detects that collision is not covered and
fails loudly instead of publishing an unsafe new owner.
**This is not yet a fully recoverable transactional movement
barrier**: a true teleport/deferred movement policy must stage
the destination *before* committing gameplay movement, so
no partially advanced game tick can ever result.

The automatic reconciliation does not park ActorHost's gameplay
components and does not make `RegionLifecycleRuntime` active
without a separate explicit setup. It is a W1 source-of-truth
ownership slice, not an open-world offscreen simulation or disk
region pager.

### Tests, status and performance limits

`tests/world/actor_region_lifecycle_test.cpp`, CTest
`actor_region_lifecycle_test`, covers a real GameCore
end-of-tick automatic car transition, independent peers and
checksums, a 100-region occupied-car trace, a passenger from a
different source region, native ECS save/load, COW rollback,
a dynamically spawned/removed loot entity, tombstone survival
across entity recycling, and no-opt-in legacy saves.

`region_entity_ledger_test` also tests all-or-nothing mixed
previous owners, sorted revisions, no-op relocation and a stale
source-owner rejection.

**Not yet run:** these CTests and the Mac/KINGSTON full suite.
Every-tick scanning of the registered actor pools and all
ledger tombstones is an intentionally simple correctness policy;
the production large-world system must replace it with
event-driven region ownership indexes and bounded disk-backed
journals. Gameplay spawn hooks, collision admission for
teleports, lifecycle of all actor pools, offscreen clock,
large-world memory and performance measurements remain W1 work.


## Stage 09 — pre-tick destination admission for actor/car teleports

`engine/world/RegionMoveRequests.h` provides an **opt-in persisted ECS
resource** `RegionMoveRequests` (schema v1; maximum 64 pending objects).
The queue contains a map keyed by `Entity::packed()+1` to the requested
destination in fixed-point world coordinates, plus an applied counter.
The +1 encoding includes a first Entity whose packed handle is zero.
The portable Archive codec refuses duplicates, invalid keys and
over-budget requests and decodes atomically. A native-ECS load validator
rejects pending requests whose Entity is no longer alive.

On a world-space `GameCore` map with `"regionOwnership": {}`,
`ActorHost::requestRegionMove(sim,entity,destination,error)` stages a
**gameplay-authorized** movement request. It supports a live
`CharacterBody` not riding in a vehicle, or an arcade
`actor::Vehicle` with its `Vehicle.driver` and
`CharacterBody.vehicle` occupants as an indivisible group.
The API rejects missing/dormant identities, overlapping requests,
invalid world bounds, or requests for a passenger independently of
its car. It **does not** directly mutate a position. Typed
`vehicles::VehicleDrive` teleports remain intentionally unsupported
until the same transaction can update their authoritative controller
position, seat pose and physics safely.

### The two-phase admission contract

1. **Stage** an authoritative target in the native ECS queue.
2. **Prepare**: before the next simulation step consumes its pending
   commands, `ActorHost::readyToStep` includes each staged
   destination (4-unit radius) in the existing
   `CollisionStream::prepareRegions` interest volume, in addition to
   actors, vehicles, projectiles and respawn bounds. For async maps
   an unavailable destination blocks the step. No RNG, tick, command,
   canonical actor pose, or ECS ledger ownership changes on that
   refused step.
3. **Admit**: after collision/nav readiness succeeds,
   `readyToStep` captures a host-local *derived* copy of the
   particular destination requests it just validated.
   This cache is neither serialized nor hashed. It is reset before
   every ready check and recomputed following rollback.
4. **Commit**: the `actor.regionMoves` Simulation system runs before
   the existing ActorHost gameplay systems. It validates the entire
   admitted actor/car group again, then changes the existing
   `CharacterBody.pos` / `Vehicle.pos` components by the requested
   delta. Applied requests are removed from the persistent queue;
   new requests inserted by a gameplay command or system **after**
   the `readyToStep` check remain pending for a later tick.
5. **Reconcile**: the already existing
   `actor.regionOwnership` system follows normal gameplay and
   atomically updates the W1 ledger for the newly moved car and
   passengers.

This avoids the W1.8 failure mode where the post-movement region
synchronizer discovers a teleport into a collision chunk that was never
admitted. The stage only protects movements issued via
`requestRegionMove` — **direct, unsupported writes to
`CharacterBody.pos` / `Vehicle.pos` by other gameplay code are
not intercepted** and may still outpace collision streaming.
Game commands arriving during the tick can stage a request, but
cannot apply that new destination until it has passed a *subsequent*
`readyToStep`. It is the caller's responsibility to authorize
which player or game rule may request a teleport.

### Tests, exclusions, and known limits

`tests/world/actor_region_admission_test.cpp` is registered as
`actor_region_admission_test` (CTest World/ECS/NETWORK). It
uses actual GameCore, ActorHost and hashed asynchronous collision
chunks with a deliberately unavailable remote chunk to assert:
blocked tick and unchanged RNG/commands/checksum/position,
eventual exact command processing after loader recovery, two
independent peer checksums, native save/reload, a late in-tick
request waiting for a new admission and a grouped arcade car move.

The queue is bounded to 64 target entities. It does not contain a
fully typed transport movement contract, a transactional ECS tick
rollback or an outdoor streamed NavMesh. It also does not remove
W1.8's full-scan ownership reconciliation cost. Actual Mac/KINGSTON
CMake+CTest execution **has not been completed** as part of this
source stage; W1 remains partial and PR #116 Draft.


## Stage 10 — typed VehicleDrive pre-admitted teleport and seat placement

The same `ActorHost::requestRegionMove` + pre-tick
`readyToStep` admission protocol from W1.9 now supports
**typed vehicles** driven by the existing
`vehicles::VehicleDrive` controller, in addition to
on-foot `CharacterBody` and legacy arcade `Vehicle`.

No second vehicle model is created. The implementation reuses:

- `vehicles::VehicleDrive::controller` as the authoritative typed
  physical state and `lib::mechanics::respawnVehicle` to move it
  while zeroing motion and preserving supported controller/gear state.
- `ActorHost::drive_` and the existing `VehicleType::seats`
  definitions, `lib::mechanics::vehicleWorldPoint` for exact
  passenger placement, and `actor::CharacterBody` as each
  passenger's canonical position/velocity.
- `vehicles::VehicleOccupant` reverse links and
  `ActorHost::vehicleRegionGroup` for seat membership, together
  with `RegionEntityLedger` mixed-owner atomic group migration.

### Validation and movement

`ActorHost::typedRegionMoveGroup` is a read-only preflight
used by `requestRegionMove` and again by the commit system. It
requires a live typed controller and its matching registered type
definition; valid seat slots; exact slot→ECS mapping; no missing
driver from the seat map, extra reverse-linked occupant, or
`CharacterBody.vehicle` referring to the wrong car. It also
inherits stable-ID/dormant/overlapping group checks from W1.9.

A typed-vehicle request waits for collision and derived navigation
to be ready **before Simulation::step consumes commands or RNG**.
A new request added during the tick cannot inherit stale admission.
On a verified tick, `ActorHost::placeTypedRegionMove` applies the
shared `respawnVehicle`, synchronizes `actor::Vehicle.pos/yaw/velocity`
with the controller, places occupied characters at the new seat
world positions, and synchronizes existing
`vehicles::VehicleOccupant` reverse component poses.
`ActorHost::tickVehicles` remains the *single* normal
physics/seat simulation. The post-tick `actor.regionOwnership`
then changes the car and occupant ledger owners together.

Controller motion is deliberately reset as part of a teleport;
this is **not** a physics-preserving cross-cell movement for a
car already driving at speed. Ordinary driving remains in
`vehicles::stepVehicles`, guarded by the existing collision
stream admission. External direct writes by other subsystems
are still outside W1.9/10's staged teleport API.

### Authored regression coverage

The existing `actor_region_admission_test` CTest now adds a
**real** `GameCore` map with `actor.vehicleTypes` and a
`VehicleDrive`-typed car, driver and passenger seats. It tests
failed remote collision loading with unchanged tick/RNG/checksum/
typed controller pose; eventual successful admission for two
independent peers; seat and controller consistency; one region
owner for the car+occupants; v10 save and checksum restore;
COW rollback; refused independent passenger teleport; and
refusal of mismatched seat/backlink relationships.

The fixture and production code are committed, but this stage's
native C++ test **has not been built or executed on Mac/KINGSTON**.
Its source assertions cannot be treated as passing test results.

### Still incomplete

- All arbitrary direct position or teleport writes from game logic
  must eventually pass through a shared admission/transaction
  protocol, not only `ActorHost::requestRegionMove`.
- Runtime physics cross-region travel may need larger swept
  collision/NavMesh envelopes at extreme speeds.
- This stage does not park the complete live vehicle/driver
  component graph or make every actor system dormant-aware.
- Outside-world disk region journal, cold population clocks,
  memory paging, cross-host net replay and benchmarking remain.
- C++ allocation failures during a multi-component commit are
  not guaranteed exception-atomic. Use the staged prevalidation
  at a safe ECS structural boundary.


## Stage 11 — round reset and shared race-respawn collision admission

Pre-tick admission now covers **engine-owned implicit teleports**, not just
explicit `ActorHost::requestRegionMove` requests.

The shared `vehicles::VehicleRace` API exposes
`possibleRespawnPositions(const World&)`, a **read-only** set of world
positions for every active racer. It combines the current respawn destination
(the last passed checkpoint, or starting grid) with the **next checkpoint**
that might be crossed during the coming tick. The race owner's
`lm::raceRespawnPoint` is reused; this query never advances race progress,
changes the controller, draws graphics, or consumes deterministic RNG. Results
are sorted and deduplicated. Finished racers contribute no new requests.

When `regionOwnership` is opted in on a streamed world-space game,
`ActorHost::readyToStep` adds those positions to its existing
`CollisionStream::prepareRegions` region demand. The same pre-tick
query also includes `initialVehicles_`, the host-owned original map
positions used by the round-reset logic. Ordinary character respawn
positions were already pinned by the existing `spawns_` loop.
A missing chunk in **any** of these requested destinations blocks the next
simulation step *before* the ordinary ActorHost systems consume commands,
tick or RNG.

### API

```cpp
// No speculative simulation or mutation; host-only collision interest.
const auto possible = race->possibleRespawnPositions(sim.world());
// ActorHost::readyToStep pins these via its existing CollisionStream
// whenever a world-space map opted into W1 regionOwnership.
```

The new pure-policy CTest `race_region_respawn_gate_test`
(`tests/world/race_region_respawn_gate_test.cpp`, linking
`aiverse_vehicles`) constructs real race state and checks current grid,
next checkpoint, previous checkpoint, finished racer exclusion and
no state mutation from asking for candidates.

The existing `actor_region_admission_test` gains an opt-in real
ActorHost + async collision fixture in which a vehicle begins at a
remote original spawn and is then moved near the players. The original
reset destination must still be prepared before a step can advance.
These are **written tests, not passed test results**: Mac/KINGSTON
build and execution are outstanding.

This is intentionally a conservative **fail-closed** policy. Several
distant checkpoints can be simultaneously required; on tight collision
residency budgets, a race may stall rather than simulate a missing
respawn destination. A bounded exact-next-respawn demand and capacity-aware
admission policy are needed before production large-track scalability.
The existing race/vehicle state machine still owns the actual respawn.
Other direct movement writers (custom scripts, host-native teleports,
some air insertion and nonstandard respawn systems) are **not** universally
rerouted through the W1.9 staging API.


## Stage 12 — bounded race preloading and collision budget diagnostics

W1.11's race admission conservatively pinned a current respawn and the next
checkpoint for every active racer. On large tracks, the *next* checkpoint
can be unreachable within the upcoming fixed tick, wasting the finite
collision-residency budget and blocking gameplay when other required
regions cannot fit.

`VehicleRace::imminentRespawnPositions(world,maxTravel)` is a read-only,
opt-in **tick-bounded** alternative to
`possibleRespawnPositions(world)`, which remains unchanged:

- It always preserves the current respawn destination (last passed
  checkpoint or grid).
- It includes a next checkpoint only when the current physical
  `VehicleDrive::controller.position` lies within
  `maxTravel + gate.radius` of its gate centre, so that a legitimate
  accepted controller step can reach its aperture during this tick.
- Missing/invalid controller data or an unmatched previous race sample
  falls back to pinning the next checkpoint, never guessing that it is
  unreachable. Zero/negative maxTravel retains old full-candidate behavior.
- The result is sorted, deduplicated and changes **no** race/vehicle/RNG
  state. A pure policy source test covers distant, close and stale poses.

Opt-in world-space `ActorHost::readyToStep` now uses a conservative
travel bound `kVehicleMaxSpeed * dt + 4m`. The current respawn target
remains mandatory. Only the *additional*, physically unreachable next
gate can be omitted. This preserves the existing shared race-policy
owner and avoids a second simulation system.

The previous unconditional `initialVehicles_` pin is now also restricted
to the due bomb/round-reset tick:
`config_.mode=="bomb" && config_.systems.enabled && match.end &&
tick>=match.end`. Ordinary gameplay no longer retains a remote original
vehicle spawn unnecessarily.

### Impossible simultaneous residency

New `CollisionStream::canFitRegions(regions,error)` is a read-only
preflight over the *same* deduplicated static manifest chunks used by
`prepareRegions`. It compares the requested cell count against
`maxResidentChunks` and the sum of reserved chunk bytes against
`maxResidentBytes`, using overflow-safe subtraction.

When the necessary pins can **never** fit simultaneously, the host
immediately rejects the next tick and exposes
`ActorHost::collisionAdmissionError()` as a host-local diagnostic
(`"collision pin set exceeds maxResidentChunks"` or
`"collision pin set exceeds maxResidentBytes"`). It does not schedule
additional async loads, advance the tick, consume commands/RNG or change
the ECS checksum. A merely unfinished async load is still retried
normally and has no impossible-budget diagnostic.

This **does not increase** the configured budget, omit mandatory
collision, or make a physically impossible pin set runnable. Host code
must change its gameplay interest/region configuration, schedule
destinations differently, or increase the manifest budget. Capacity
diagnostics remain derived, not saved/hashed.

### Regression and current acceptance status

`race_region_respawn_gate_test` now covers
`imminentRespawnPositions` (far gate pruned, close gate restored,
stale controller conservative, old full query unchanged).
`actor_region_admission_test` now covers a two-chunk manifest with
`maxResidentChunks=1`, diagnostic, unchanged state and successful
retry after replacing the target with one in the resident region.
Its bomb round-reset fixture was updated to verify the remote original
car position is needed **only when reset is due**.

These are authored CTest assertions, not executed results.
No Mac/KINGSTON native build or CTest was run. The branch remains WIP
and must pass the merged-tree suite before release or main merge.

Remaining limits: the fixed maximum-controller-speed envelope is for
standard accepted motion and cannot protect scripts that directly mutate
positions by arbitrary distances; those require explicit staged admission.
A race with several *mandatory* distant respawns exceeding capacity
still blocks; next-stage policy needs deterministic residency partition
or larger manifest budget, not unsafe eviction.


## Stage 13 — live-ID index and trusted movement requests by stable identity

The persistent `RegionEntityLedger` now maintains a **derived sorted
`liveIds_` index** alongside its authoritative `records_` map. The index
is updated when an identity is spawned or tombstoned and is rebuilt
transactionally from live records after an Archive/native ECS load. It is
not written as an independent schema, not hashed separately and does not
change existing on-disk bytes or ledger revision order.

```cpp
const auto* ledger=sim.world().resourceConst<world::RegionEntityLedger>();
for(world::RegionEntityId id:ledger->liveIds()) {
    // Historical tombstones no longer appear in this loop.
}
```

The world-space `ActorHost::synchronizeRegionOwnership` removal pass
now iterates a snapshot of `liveIds()` rather than **every** historical
ledger record. This makes the retirement scan proportional to the current
live W1 population instead of the total number of objects that have
ever existed. A test explicitly adds/removes 2000 IDs and verifies
`liveCount()==0`, then adds a new ID and round-trips the archive.

This does **not** yet make region updates fully event-driven:
`CharacterBody`, `Vehicle` and `Loot` active ECS pools are still
scanned to identify new actors and changed positions, and the live-ID
retirement pass still touches every active tracked identity. A
production million-actor framework needs stable ECS spawn/destroy and
movement dirty notifications, plus an indexed on-disk region journal.

### Trusted script/native movement boundary

`ActorHost::requestRegionMoveByStableId(sim,id,destination,error)`
is a host-facing entry point for **trusted, already authorized**
gameplay script/native logic. It resolves a W1 durable ID from the
`RegionEntityLedger`, requires a currently live ledger record and
matching `RegionStableIdentity` on its materialized ECS Entity,
and then delegates to the *existing* W1.9–12
`requestRegionMove` staging, collision/nav admission and
authoritative fixed-tick movement pipeline.

```cpp
// After verifying script/player/mission authority:
actor::host(sim).requestRegionMoveByStableId(
    sim,stableId,{160_fx,2_fx,48_fx},&error);
```

A zero ID, tombstoned ID, recycled ECS handle, missing live binding,
bad target region or overlapping occupied-vehicle request fails
before adding any queued movement. This API does **not** install a
new network opcode or automatically grant untrusted clients teleport
authority. The world-space host does not currently load grid-native
modules or a common embedded Lua script engine, so an individual game
must connect its script callback to this validated C++ boundary.
It does not intercept unrelated direct writes to actor/car positions.

Tests extended: `region_entity_ledger_test` validates live-index
maintenance, thousands of tombstones and archive rebuild;
`actor_region_admission_test` validates valid and invalid stable-ID
move staging in a real world-space GameCore.

**Tests authored, not run** on the required Mac/KINGSTON build; the
feature remains W1 partial and PR #116 Draft.


## Stage 14 — revision-indexed bounded change delivery

The native ECS `RegionEntityLedger` now maintains an additional
**derived** `revisionIndex_` mapping each record's *latest* revision
to its stable entity ID. It is updated when IDs are spawned, transferred
(including mixed-origin car/occupant groups) or tombstoned, and rebuilt
transactionally when decoding a saved ledger. The persistent schema
remains v1 and the serializer still writes only `records_` and
`lastRevision_`; no derived index changes the saved bytes or
authoritative checksum.

`since(revision)` now uses
`revisionIndex_.upper_bound(revision)` instead of scanning every
historical record and sorting them. New
`pageSince(revision,limit)` returns a
`RegionEntityLedger::ChangePage` with
`changes`, `nextRevision` and `more`, ordered by current record
revision. Memory use and delivery effort per request are bounded by
the requested page size. The index itself scales with the number of
all retained records, including tombstones.

`RegionChangeCursor` provides a small host-side acknowledged observer
that calls a callback for each changed record, advances only after
the callback returns true and allows explicit `reset()` after a
game load/rollback:

```cpp
world::RegionChangeCursor observer;
const bool caughtUp = observer.consume(
    *ledger, 128,
    [&](world::RegionEntityId id,
        const world::RegionEntityRecord& latest) {
        return publishRegionChange(id, latest); // bool ACK
    });
```

The example's `publishRegionChange` is a game/provider callback
supplied by its owner, not an engine disk writer.

**Important latest-state semantics:** Each ID appears at most once
in the index with its **current/latest record**. If one actor moves
through A→B→C before an observer polls, the observer sees the latest
C record, not two discrete movement events. This is suitable for
convergent snapshots, index updates and region invalidation, but is
**not a lossless event log or crash-safe per-region disk journal**.
A persisted journal must record each transaction before committing it
and correctly handle fsync/checkpoint recovery.

The existing `ActorHost` still scans live CharacterBody/Vehicle/Loot
pools for direct field changes because core ECS has no generic
`onChanged` callbacks for arbitrary mutable component fields.
Stage 14 makes **ledger change consumption incremental**, not
general gameplay movement detection. Do not replace the actor
reconciliation with `pageSince` alone; that would miss new
positions that never reached the ledger.

The `region_entity_ledger_test` source now checks
pagenation over repeated transfers, mixed owners, tombstones,
transactional archive reconstitution, acknowledged callback failure
and retry, and cursor reset. No Mac/KINGSTON native build/CTest
was run here, so these are authored assertions rather than
passing-test evidence.
