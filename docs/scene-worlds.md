# AIVERSE — multiple scenes and multiple ECS worlds

Status: **Stage 2 API and async coordinator implemented, native tests pending**, branch
`feature/scene-runtime-multiworld`. Handoffs:
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md` and
`docs/agent-handoffs/scene-worlds-stage2-2026-11-19.md`.

## Key decision: a Scene is not an ECS World

- **ECS World** (`engine/core/ECS.h`) is a container of Entity IDs, component
  pools and World resources.
- **Simulation** (`engine/sim/Simulation.h`) owns exactly one World plus a
  deterministic tick, RNG, ordered commands and snapshot/replay state.
- **Scene** (`engine/scene/SceneRuntime.h`) is a named, independently
  unloadable **set of entities in one ECS World**. Additive levels share the
  same Entity allocator, systems, physics context and tick.
- **SimulationHost** (`engine/scene/SimulationHost.h`) owns several completely
  independent Simulations. The host does not allow raw Entity handles to cross
  World boundaries.
- **world::WorldScene** (`engine/world/WorldScene.h`) streams geometry and
  presentation chunks. It is **not** an ECS World or the SceneRuntime. Reuse it
  for physical data/residency when a future SceneAsset loader is integrated.
- **SceneHierarchy** (`engine/sim/SceneHierarchy.h`) is the existing
  Parent/LocalTransform/WorldTransform graph **within one ECS World**, not a
  scene manager. Cross-scene parent links are allowed within a World.
  Cross-world Parent pointers are forbidden.

### Recommended ownership layout

```text
Process / Engine
  Shared immutable asset cache; Device / Renderer
  SimulationHost
    WorldHandle A -> Simulation A -> ECS World A
      persistent scene (audio, game systems, globals)
      city scene (additive)
      house interior scene (additive)
      streamed chunks (WorldScene / world.async)
    WorldHandle B -> Simulation B -> ECS World B
      independently stepped preview / replay / match
```

**Normal game:** one ECS World plus many scenes. Creating a second ECS World
for every room or streamed chunk wastes state, breaks ordinary Entity links
and complicates simulation/network/physics. Use a second World for an
independent simulation (editor play clone, separate multiplayer match,
replay/preview, isolated minigame, authoritative/client simulation pair).

## Implemented API (Stage 1)

`engine/scene/SceneRuntime.h`:

- `SceneId`: nonzero, monotonically increasing ID scoped to one ECS World.
- `SceneOwner`: authoritative ECS component identifying entity membership.
  Entities without it are host-owned and will **not** be unloaded by scenes.
- `SceneState`: authoritative World resource containing asset identifiers,
  Loading/Active phases, persistent bits, next ID and pending replacement.
  `std::map` gives canonical deterministic traversal order.
- `SceneRuntime::beginLoad(asset, mode, persistent)`: register scene intent.
  Does **not** read assets. Modes: `Additive`, `Replace`.
- `SceneRuntime::createEntity`, `adopt`, `transfer`, `ownerOf`: scene
  membership API. `adopt` rejects an entity already owned by any scene;
  `transfer` is explicit.
- `SceneRuntime::activate(id, hierarchy)`: publish a ready scene, optionally
  removing prior nonpersistent scenes for Replace. The old scene is kept until
  activation. Persistent scenes survive Replace.
- `SceneRuntime::cancel`: abort an unfinished scene and discard its entities.
- `SceneRuntime::unload`: remove all scene-owned entities and metadata. When
  passed the active SceneHierarchy, it detaches nodes **before** destruction;
  children from other scenes become scene hierarchy roots.
- `registerScenePersistence(sim)`: register the version 1 component/resource
  schemas used by native binary save and checksum. Call exactly once **inside
  GameModule::setup**, before persistence registration is sealed.

Example from a native project's `GameModule::setup`:

```cpp
#include "engine/scene/SceneRuntime.h"

void MyModule::setup(aiverse::Simulation& sim,
                     const aiverse::Json& rules,
                     const aiverse::Json& initialMap) {
    aiverse::scene::registerScenePersistence(sim);
    aiverse::scene::SceneRuntime scenes(sim.world());
    auto shared = scenes.beginLoad("levels/global", aiverse::scene::LoadMode::Additive, true);
    auto city = scenes.beginLoad("levels/city");
    // Populate each scene with owned entities in your asset-instantiation layer.
    auto sun = scenes.createEntity(shared);
    auto npc = scenes.createEntity(city);
    (void)sun; (void)npc; (void)rules; (void)initialMap;
    scenes.activate(shared);
    scenes.activate(city);
}
```

Asynchronous loader/presentation must not call ECS structural APIs from a
worker thread: perform CPU preparation in workers, then instantiate and
activate at a deterministic tick barrier on the owning simulation thread.
Do not invoke `unload` during `World::each` or concurrent scheduler lanes.
Pass `SceneHierarchy*` when a hierarchy is installed. The host is responsible
for releasing scene-specific physics, navmesh and GPU resources at a matching
lifecycle barrier; **this stage does not yet do that automatically**.

A scene record in `Loading` is *not* an automatic ECS query filter: it is
the host's responsibility to make the prepared entities eligible at
activation, or to defer instantiation until ready. Thus `beginLoad` is
a **scene lifecycle primitive**, not yet full asynchronous level streaming.

There may be only one pending `Replace` request per ECS World; a concurrent
replacement request is rejected until `activate` or `cancel`. Scene IDs are never recycled after cancel/unload **within one simulation
 timeline**. Rollback restores the allocator counter, so external async
 tickets from an abandoned timeline must also be invalidated by a separate
 host-owned epoch; Stage 2 must implement that protection.

## Multiple independent Simulations

`engine/scene/SimulationHost.h`:

```cpp
aiverse::scene::SimulationHost host;
auto first = host.insert(std::move(firstSimulation));   // set up before insertion
auto second = host.insert(std::move(secondSimulation));
host.setPaused(second, true);
host.stepAll();             // only first steps
host.setPaused(second, false);
host.advanceAll(0.05);      // each runs its own fixed tick accumulation
auto* simulation = host.get(first);
host.erase(first);          // invalidates first's WorldHandle
```

`WorldHandle` is `(slot index, generation)`; `WorldEntityRef` is
`(WorldHandle, Entity)`. Do not store or resolve raw `Entity` across worlds.
IDs are checked on every host lookup and stale handles fail after removal
and slot reuse. Slot generation wrap retires the slot permanently.

The host is a single-owner sequential dispatcher, not parallel game-world
simulation. It rejects insert/erase/pause operations during a tick callback.
Worlds keep separate RNG, command queues, tick counters, rollback rings and
gameplay data. The render/backend asset cache may be shared, but mutable ECS
resources must not be shared between worlds without an explicitly designed
message/snapshot bridge.

## Save / rollback contract

A `SimSnapshot` copies the ECS World, so `SceneOwner` and `SceneState`
participate automatically (including COW). To persist them to binary disk
saves, opt-in projects **must** call `registerScenePersistence` before setup
is sealed. This contributes `aiverse.scene.owner` and
`aiverse.scene.state` schemas to native ECS v10 saves/checksum. Legacy
RTS/FPS projects that do not opt in keep their existing formats unchanged.

Deterministic *gameplay* scene transitions must execute as canonical scheduled
commands on the same tick for every lockstep peer. Local asset readiness must
only block wall-clock progress through `readyToStep()`, not select an
activation tick based on worker completion speed. After deserializing:
rebuild `SceneHierarchy` as usual; rebuild/publish renderer/physics caches
from resident scene assets, and do not serialize GPU objects or background
jobs.

## Stage 1 verification

CTest targets registered in the top-level CMake test group:

- `ecs_scene_runtime_test`: additive/replace/cancel, persistent scene
  protection, cross-scene hierarchy unlink, ID generations, COW, and native
  save/load/checksum.
- `ecs_multiworld_test`: separate Simulation ticks, per-world pause,
  cross-world identity and stale WorldHandle rejection.

To run on the user's native workstation (from this branch):

```sh
git fetch origin feature/scene-runtime-multiworld
git switch feature/scene-runtime-multiworld
git pull --ff-only origin feature/scene-runtime-multiworld
cmake -S . -B build/scene-worlds -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-worlds --target aiverse_ecs_scene_runtime_test aiverse_ecs_multiworld_test -j8
ctest --test-dir build/scene-worlds --output-on-failure -R '^(ecs_scene_runtime_test|ecs_multiworld_test)$'
```

Tests were **added but not executed natively** in this agent environment.
Do not mark Stage 1 production-ready until compiler/CTest results are
recorded in its handoff, and the full regression suite has passed.

## Future stages (not yet implemented)

1. SceneAsset format/registry with stable asset keys, validated prefab IDs,
   dependency graph and references.
2. Async CPU preparation and main-thread ECS instantiation; unpublish hooks
   for WorldScene, collision, PHYSIS/TerraWeave, water/NEREID, SpectraFX and
   audio. Shared immutable assets referenced rather than duplicated.
3. Explicit activation/loading barriers, preloading, cancellation,
   deterministic scene transition commands and scene-specific world events.
4. Scene hierarchy/editor tree and additive/unload UI, prefab overrides,
   scene validation and serialization/migration.
5. Network replication interest across scene regions, persistent-entity
   transfer, scene-travel save/load and multiworld tooling.
6. Cross-genre RTS/FPS/native integration & profiling; large-world streaming
   stays chunk-based rather than creating a new ECS World for every chunk.

Until a verified migration, do not rewrite RTS/FPS map loading, clone a second
canonical gameplay state or change the meaning of the legacy world-space
`Transform`. Follow `docs/ecs-agent-handoff.md` and
`docs/ecs-scene-hierarchy.md` for concurrent ECS work.


## Stage 2 — typed SceneAsset and async CPU loader (implemented, not yet tested)

New public headers:
- `engine/scene/SceneAsset.h` — strict, stable v1 manifest for logical
  asset key, dependency keys and prefab instances (scene-local ID, parent
  ID and JSON properties). Decoding validates key path traversal, missing
  parents, cycles, duplicate IDs/dependencies and count bounds. Dependencies
  and objects are sorted into canonical order.
- `engine/scene/SceneLoadCoordinator.h` — async manifest read and parse on
  a worker using `std::async`; no ECS writes outside the main-thread barrier.
  Explicit `poll()`, `ready(ticket)` and `commit(ticket)` API. Manages
  cancellation and stale worker completions, shared refcounted resource
  adapters, prefab spawner callbacks and SceneHierarchy parent linking.

### Manifest example (`scenes/city`)

```json
{
  "version": 1,
  "id": "scenes/city",
  "dependencies": ["models/house.glb", "materials/brick.mat"],
  "objects": [
    {"id": 100, "prefab": "prefabs/house", "properties": {"x": 10}},
    {"id": 200, "parent": 100, "prefab": "prefabs/window",
     "properties": {"x": 3}}
  ]
}
```

The reader receives the logical `id`; it may resolve it to any content-file
layout or pack entry. Manifest ID must exactly match the requested key.
The scene manifest is capped at 4 MiB *after* reading, so an untrusted
streaming reader must impose its own read limit. Format v1 is deliberately
minimal: it does not yet prescribe how prefab properties map onto game
components. The native project supplies this as a callback.

### Native C++ usage

```cpp
#include "engine/scene/SceneLoadCoordinator.h"
using namespace aiverse;
namespace sc = aiverse::scene;

// The module registers aiverse.scene.owner + aiverse.scene.state during setup.
// Bind this coordinator to the existing World and SceneHierarchy.
// Reader: worker-thread safe, returns manifest bytes through 'out'.
// Resource adapter: owner-thread acquire and release by immutable asset key.
// Spawn: owner-thread instantiation of one prefab on the new Entity.
sc::SceneLoadCoordinator coordinator(
    sim.world(),
    readSceneAsset,
    sc::SceneResourceAdapter{acquireSharedAsset, releaseSharedAsset},
    instantiatePrefab,
    &sceneHierarchy);

auto ticket = coordinator.request("scenes/city", sc::LoadMode::Additive);
// During normal host frames: coordinator.poll(); observe coordinator.ready(ticket).
// On an authoritative, deterministic tick/structural barrier, only after all
// participating peers have content ready:
if(coordinator.ready(ticket)) {
    const bool activated = coordinator.commit(ticket);
    // A failure leaves preexisting scenes intact. Handle error(ticket).
}
```

The renderer, model cache, collision, navmesh, TerraWeave, PHYSIS, NEREID,
Prisma and SpectraFX are **not automatically integrated**. Host adapters
`acquireSharedAsset`/`releaseSharedAsset` are intended to reference
these systems; dependency acquisition happens only on the owner thread.

`SceneLoadCoordinator` keeps one reference for each dependency in each
resident scene, but calls the adapter's `acquire` only for the first
reference and `release` for the last. Independent Simulations may share
a common external asset manager via their adapters. The coordinator's
own refcount table is per ECS World, not a global GPU cache.

### Determinism, loading barriers, rollback

**Never call `commit` immediately when a future finishes on a multiplayer
client.** Worker completion is only a readiness signal. The host must schedule
a deterministic scene-transition command/tick, hold the simulation through
`GameModule::readyToStep` if immutable content is absent, then commit at
the agreed structural barrier. `poll()` only updates host-owned job status.

When a committed scene replaces another, prefab creation and resource
acquisition happen first; previous nonpersistent scenes are destroyed only
after new content has passed preparation. If a prefab callback fails,
newly-created scene-owned entities and acquired leases are rolled back.
For this reason, prefab callbacks must touch only the new entity and avoid
non-transactional effects on external/global gameplay state.

After `Simulation::loadState` or a successful binary `deserialize`, call
`invalidateAfterRestore()`. This invalidates ticket epochs and discards
outstanding results from the abandoned rollback timeline, then diffs the
non-authoritative resource leases against the restored `SceneState`: a scene
that is still Active with the same asset keeps its leases (no release or
restream); leases of every other scene are released. Use
`queueRestoredResidency()` to asynchronously reacquire dependencies for Active
scenes that are not resident **without re-spawning their already restored ECS
entities**; it also schedules a full load job for every restored Loading
record (its old job died with the epoch), so a restored `pendingReplace` can
be committed or cancelled. Wait for and commit those tickets at the
deterministic barrier before simulating physics that depends on them.
Terminal job records (Committed/Unloaded/settled Failed) are pruned by
`poll()` beyond `kMaxTerminalJobs` (64); older tickets then report Unknown.
`SceneHierarchy::rebuild(world)` remains necessary after rollback.

Limitations:
- An authoritative scene still has an explicit Loading state. A loading
  scene is not automatically filtered out of arbitrary ECS queries: instantiate
  at the activation barrier or explicitly gate systems until activation.
- Scenes loaded before a coordinator's creation must be rehydrated using
  `queueRestoredResidency`; the coordinator is not an automatic watcher
  for direct `SceneRuntime` changes.
- An external `SceneRuntime::unload` performed without the coordinator cannot
  adjust resource leases. Use `SceneLoadCoordinator::unload` when it owns
  presentation dependencies.
- Restored *Loading* scene records get a fresh full job from
  `queueRestoredResidency()`; the host must commit or cancel that ticket.
- `std::async` tasks cannot be forcibly interrupted. Cancel is nonblocking,
  tombstones are joined by `poll()`, and coordinator destruction can wait
  for remaining work. Reader callbacks must be thread-safe and lifetime-safe.
- Asset reader/backend must enforce its own memory budgets and thread limits.
  The coordinator limits in-flight jobs (default: four) but does not yet
  impose frame-time or GPU-upload quotas or dependency graph traversal.
- Production multiworld physical-simulation coupling, replication and
  editable scene/prefab authoring are later stages.

### Validation

Registered test: `ecs_scene_load_coordinator_test`, covering manifest
validation, worker/owner thread separation, dependencies shared between
scenes, parent hierarchies, failed replacement, cancel with late worker,
COW rollback and scene residency rehydration. Native execution remains
unverified as of this document.

```sh
git fetch origin feature/scene-runtime-multiworld
git switch feature/scene-runtime-multiworld
git pull --ff-only origin feature/scene-runtime-multiworld
cmake -S . -B build/scene-worlds -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-worlds --target aiverse_ecs_scene_load_coordinator_test -j8
ctest --test-dir build/scene-worlds -R '^ecs_scene_load_coordinator_test$' --output-on-failure
```

For more complete next steps, see the Stage 2 agent handoff.


## Shared/Static prefab integration (isolated integration branch)

The separate `feature/ecs-static-prefab-scene-integration` branch
merges real SceneRuntime and immutable prefab catalogs without modifying
their original feature branches. Use
`engine/scene/PrefabStaticSceneBinding.h` to turn
`storage::PrefabStaticCatalog<T>` into the coordinator's
`SpawnPrefab` callback, so additive scenes create just a
`StaticRef<T>` per entity and share one sealed config table even across
independent ECS Worlds. See
`docs/ecs-static-prefab-scene-integration.md` and
`docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md`.
**Combined CTest remains native-unverified; do not merge to main yet.**

## Stage 7 — additive static collision ownership

The isolated integration branch now provides opt-in collision namespaces
for multiple active `CollisionStream` instances in one
`world::SpatialWorld`, preserving the legacy 3-argument API. A host
declares a sorted `SceneCollisionNamespaces` catalog before loading,
then uses `SceneCollisionBridge` as a dependency adapter to
`SceneLoadCoordinator`. Each actual async stream receives a unique
high-ID range. Unloading one scene removes only its own colliders.
Required collision regions must be ready before advancing the ECS tick.

Read `docs/scene-collision-namespaces.md` for the full contract and
native tests. Unlike previously noted limitations, **static ID
namespace support is now implemented in source**, but not yet verified
by native CTest. Cross-stream terrain seams, multi-platform GPU and
complete FPS/RTS adoption still need additional validation.
