# MotionWeave — native project prefab/SceneAsset visual bootstrap (Stage 178)

Date: 2026-11-20. Branch: `feature/universal-ecs-animation-pipeline`.
Status: source-authored; compile, runtime, CTest and GPU validation **NOT RUN**.

## Contract and ownership

`engine/view/MotionWeaveScenePrefabBindings.h` is a genre-independent
project-supplied prefab registry and concrete synchronous dispatcher for the
existing `MotionWeaveSceneGameHost` /
`MotionWeaveSceneAssetBuilder`. It does NOT change SceneAsset v1,
Simulation, authoritative prefab spawning or SceneLoadCoordinator.

The native project first configures its normal
`scene::SceneLoadCoordinator::SpawnPrefab(World&,Entity,SceneObjectAsset,error)`
callback to spawn *authoritative* scene Entities. Independently it installs
visual-only callbacks for exactly the same logical prefab keys. Both systems
read SceneObjectAsset::id; the visual side uses decimal stable object keys,
not an ECS entity index.

Register each visual prefab with **both** callbacks:

- Fresh: `bool(const SceneObjectAsset&, ModelAssetProvider&, gfx::Device&, optional<Object>&, string&)`, which may import/compile/upload and must return a valid visual Object.
- Resident: `bool(const SceneObjectAsset&, const ModelAssetProvider&, optional<Object>&, string&)`, which can only refer to immutable Models/Graph snapshots already in the shared native provider.
- Prefabs with no visuals (triggers, markers, collision-only entities) use `registerNonvisual(key)`. Unknown keys fail closed rather than silently omitting visuals.

Use `seal("characters/base-pack@v1")` **before** activating a scene. A
sealed table cannot be modified. The pack identity must change if Model,
Skeleton, Graph or asset dependency semantics change. Share one sealed
binding and the same GameHost/GPU generation across additive scenes that
reuse that resident pack; distinct packs use distinct registrations/keys.

The callbacks are synchronous, owner-thread only and MUST NOT mutate
`Simulation::world()`, read an async worker's transient `prepared()`
pointer outside the barrier, re-enter scene commit, or retain frame-scoped
`ModelReadLease`/Prepared packets. Call the bootstrap after render command
recording for the previous frame is complete and at the game's canonical
structural/tick barrier.

## Native project wiring

```cpp
using namespace aiverse;
using namespace aiverse::view;

// Project constructs Simulation, SceneLoadCoordinator, gfx::Device,
// RenderDeviceLifetime, and MotionWeaveSceneGameHost in that lifetime order.
// SceneLoadCoordinator's independent SpawnPrefab callback is responsible
// for authoritative ECS prefab materialization; it is NOT provided by this
// visual registry.
MotionWeaveScenePrefabBindings visuals;
std::string error;
visuals.registerNonvisual("logic/trigger", &error);
visuals.registerVisual("characters/human",
    projectImportHumanVisual,      // project supplies real Model/Graph
    projectResolveResidentHuman,   // read-only, no cache mutation
    &error);
visuals.seal("characters/rig-and-graphs@v5", &error);

// Request + poll are non-authoritative preparation steps.
auto ticket = gameHost.request("scenes/area-a",scene::LoadMode::Additive);
gameHost.poll();

// Later, ONLY at the coordinated owner-thread structural barrier:
if(gameHost.ready(ticket)) {
    MotionWeaveSceneGameHost::Statistics stats;
    const bool committed=visuals.commitReadyShared(
        gameHost,ticket,device,deviceLifetime.lease(),
        projectReadModelFile,stats,&error);
    // If rejected, keep the old authoritative/visual scene and diagnose.
}
```

The `projectImportHumanVisual` and `projectResolveResidentHuman` names
above are application-supplied callbacks, **not engine built-ins**.
Their Graph publication, static child attachments and independent animated
child snapshots must already satisfy `MotionWeaveSceneAssetBuilder` and
`MotionWeaveScenePublication` constraints.

For dedicated providers use `commitReady(...)` rather than
`commitReadyShared(...)`; for shared providers the first scene creates a
real ModelAssetProvider, later scenes only borrow its existing immutable
Model/Graph resources. If the second scene needs assets not prewarmed in
the first, it rejects instead of modifying the live provider.

On any successful Replace, the existing GameHost reads the canonical
SceneState ACTIVE IDs and preserves still-active persistent scenes. On
authoritative rollback call `restoreResidencyAfterRollback()` only after
`Simulation::loadState()`; requeue residency-only tickets and commit their
visuals without respawning gameplay entities. Shutdown requires
`completeForShutdown()` with live gfx::Device and no outstanding
frame-scoped read/draw leases.

## Integration status / limitations

- The actual SceneAsset, SceneLoadCoordinator, Simulation and real shared
  GPU owner APIs are called; not a duplicate scene runtime.
- A **reusable project integration entrypoint** is implemented. This is
  not yet wired to a particular native app/GameModule's startup, because
  no application-specific prefab/model/graph asset policy was selected.
  The project must install its real spawn callback, Model reader and
  visual resolvers at bootstrap.
- No runtime content pack or prefab-to-Model mappings were invented.
- This does not generate missing models, automatically prewarm future scenes,
  fingerpint model packs, or migrate immutable shared providers across GPU
  device epochs.
- Real compile, link, CTest, live SceneAsset activation/cancel/rollback and
  Metal/Vulkan/GLES parity have NOT been executed from the GitHub-only
  environment. The updated `animation_scene_game_host_api` fixture is
  only source-authored.
- The next agent should wire one concrete project-created `GameModule`
  instance once its actual resolver and scene assets are available, then
  complete facial Control Rig, IK, morph-aware automatic LOD, followed by
  the one-pass Mac acceptance requested by the user.

Deferred Mac test target:

```sh
cmake --build build/ecs-test --target aiverse_animation_scene_game_host_api_tests
ctest --test-dir build/ecs-test --output-on-failure -R '^animation_scene_game_host_api$'
```

See `docs/agent-handoffs/animation-stage178-prefab-bootstrap-2026-11-20.md`
for complete stage provenance and remaining tasks.
