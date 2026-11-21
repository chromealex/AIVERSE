# MotionWeave shared native Model GPU residency — Stage 177

Date: 2026-11-20. Branch: `feature/universal-ecs-animation-pipeline`.
Status: **source implementation only**; Mac build/CTest, live GPU and backends NOT RUN.

## Ownership and invariants

`engine/render/SceneModelProviderResidency.h` stores the **actual** unique
`render::ModelAssetProvider` in a named, versioned registry entry. It hands
out noncopyable/move-only `Lease` values with key + monotonic generation and
tracks their reference count. This is **not** an AppearanceAsset/Model
`shared_ptr`; it retains the provider's ModelResourceCache, texture cache,
in-flight rollback buffers and CPU loader until safe GPU retirement.

`engine/view/MotionWeaveSceneOwnedHost.h` now supports both existing
dedicated-provider `stage()` and opt-in `stageShared()` for the same
independent cosmetic scene Worlds. Live and staged visual generations each
hold their own shared lease. Acquiring the replacement's shared lease before
dropping the old staged lease ensures a same-key replacement cannot retire
its provider in between. A committed scene swap releases the former live
lease **after** successful Graph/World publication, and unload, Replace,
cancel and rollback release only the corresponding leases after detaching
cosmetic readers. The **last** lease moves the unique real provider into
`SceneModelProviderRetirement` with existing RHI token / full-idle fallback.
Any previous or next scene with a matching provider key does not itself
authorize GPU destruction.

A provider key must identify a compatible immutable residency generation:
model set, skeleton/rig/Graph snapshot semantics, authoring version and
device epoch. No two different model packs may use the same key.
Never invalidate/unload/mutate a model cache shared by an active scene.
A cosmetic snapshot alone is never an ownership proof. The engine's
`ModelReadLease` protects CPU pointer lifetime; the GPU retirement token
protects asynchronous GPU submissions. Both protections remain required.

## Native GameHost integration

The actual `MotionWeaveSceneGameHost` exposes
`commitReadyShared(ticket, providerKey, build, stats, error)`. It calls the
real `SceneLoadCoordinator::prepared/commit/cancel` and reads canonical
SceneState ACTIVE IDs after successful native commit, preserving persistent
scenes under Replace. The builder signature is:

```cpp
bool build(
    const scene::SceneAsset& manifest,
    const render::ModelAssetProvider* residentOrNull,
    view::MotionWeaveSceneGameHost::Owner& newProvider,
    std::vector<view::MotionWeaveSceneGameHost::Object>& objects,
    std::string& error);
```

**First scene with key:** `residentOrNull==nullptr`. Builder prepares
a new `Owner` with the usual genre-neutral
`MotionWeaveSceneAssetBuilder`; then all Model/Graph snapshots are validated
against its actual cache. The host adopts this new owner into the registry
only after the visual build succeeded.

**Further scenes with the same key:** `residentOrNull!=nullptr`.
Builder **must leave** `newProvider` null and resolve prefab visuals
read-only against the exact resident provider. The static helper
`MotionWeaveSceneAssetBuilder::buildResident(...)` validates stable
`SceneObjectAsset::id` keys, unique IDs, and ModelReadLease ownership of
all parent/animated-child snapshots. Only models already in the shared
cache can appear. Static-child snapshots must also resolve within that
same cache when the cosmetic scene generation is staged.

Example for a project-provided resolver and prewarming policy:

```cpp
const auto buildShared =
    [&](const scene::SceneAsset& manifest,
        const render::ModelAssetProvider* resident,
        view::MotionWeaveSceneGameHost::Owner& next,
        std::vector<view::MotionWeaveSceneGameHost::Object>& visuals,
        std::string& error) -> bool {
        if(resident) {
            return view::MotionWeaveSceneAssetBuilder::buildResident(
                manifest,*resident,residentVisualResolver,visuals,error);
        }
        // newSceneBuilder already holds the correct gfx::Device, current
        // RenderDeviceLifetime lease and project-specific Model reader.
        return newSceneBuilder(manifest,next,visuals,error);
    };

view::MotionWeaveSceneGameHost::Statistics stats;
std::string error;
const bool accepted=gameHost.commitReadyShared(
    readyTicket,"characters/base-pack@v3",buildShared,stats,&error);
```

The project is responsible for guaranteeing that **every** model needed by
the second scene was resident on the first publication. If the resident
builder cannot fulfill the new scene, reject the new ticket and keep the
old scenes alive. To add new resources, publish a new versioned provider
key (or implement a separately coordinated owner-thread pack extension)
instead of mutating a shared in-flight cache.

A failed builder with a partially uploaded new provider keeps that owner
alive for GPU-safe retirement. A builder returning an extra new provider
for an occupied key is rejected and the new provider is retired. A native
commit rejection discards only the staged visual lease. An authoritative
commit success is never rolled back merely because a late cosmetic
publication callback fails.

## Lifecycle, shutdown and caveats

- Construct SceneGameHost and all cache/provider operations on the native
  scene/render owner thread. Store no bare provider pointer across frame
  boundaries, scene mutations or device recreation.
- Finish/submit the last frame and drop `Prepared`/ModelReadLease owners
  before unload, Replace, rollback, shared stage and retirement collection.
- Call `collectRetired()` after frame submission; call
  `completeForShutdown()` with a live `gfx::Device` before device/lease
  destruction. Incomplete shutdown intentionally quarantines orphaned
  provider owners instead of freeing GPU resources while potentially in use.
- Residency is scoped to **one host/one RHI device generation**. Cross-host,
  cross-device shared caches are not implemented. Residency caching after
  the last scene release is not implemented: the provider begins ordered
  retirement immediately at reference count zero.
- Reference counting protects Model owners, **not** producer-side
  AppearanceAsset/Graph publication. The project must keep snapshots
  consistent with the cache and enforce immutable sources while shared.
- This class does not change `SceneResourceAdapter` authoritative dependency
  counts. It is an independently owned cosmetic Model GPU lifetime.
- No GPU token proves completion for a new frame recorded after unload.
  Host must obey the documented structural/presentation frame barrier.

## Verification pending

`tests/animation/scene_game_host_api.cpp` now includes deferred compiler
instantiation of `commitReadyShared` with the actual native coordinator
and compile assertions for noncopyable shared leases. The target is already
registered in CMake as `animation_scene_game_host_api`. **It has NOT
been compiled or executed in this environment.**

The final native Mac pass must verify multi-scene additive and persistent
Replace, same-key replacement, staged failure, read-only resolver rejection,
last-owner GPU retirement, pending/failed/completed/no-token backends, epoch
change, two concurrent read leases, scene rollback and concrete game host
bootstrap. Run CMake/CTest, ASan/TSan/UBSan, Metal/Vulkan/GLES and the
1k/10k/100k crowd/VRAM/stall benchmarks as part of the user's single
coordinated acceptance after all feature code is closed.

See [Stage 177 handoff](agent-handoffs/animation-stage177-shared-real-model-residency-2026-11-20.md).
