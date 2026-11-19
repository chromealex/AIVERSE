# TerraWeave Stage 55 — canonical ECS rollback + native WorldScene rehydration

Date: 2026-11-19. Branch: `feature/terraweave`, Draft PR #24, base `main` integrated in previous TerraWeave merge.

## Actual commits and API

- `2b04ffc5328671b36c3fbe9df7973cbb7071d345` — NEW `engine/world/TerraWeaveEcsAuthority.h`. `TerraWeaveAuthorityResource` is the SINGLE canonical `World` ECS resource for sparse Earthworks vertex overrides, root states/age/light/water/nutrients and grass patch coverage. `registerTerraWeaveAuthorityResource(sim,cfg)` registers a stable-versioned persistent resource through AIVERSE `PersistentEcsRegistry` and places initial state into `sim.world()`. `excavateAuthoritativeEcs(World&,cfg,brush,tick)` stages excavation/root/grass calculations and publishes the new ECS resource only on success. `addTerraWeaveAuthoritativeBankSystem(sim,name,cfg,onCommit)` consumes the ALREADY COMPUTED NEREID `WaterBankErosionOutputComponent` after `registerNativeWaterPipeline` with bank erosion enabled. No water calculation occurs twice and no graphics/physics objects are created inside an ECS tick.
- `c278e4ba522ae6954b590e508bb1e965dc212967` — ECS resource layout schema **v2** with monotonic `stateRevision` independent of the soil edit revision; each changed soil/root/grass tick bumps it. Stage55 source was not built before this change, so no released v1 resource migration is implied. Resource schema ID: `terraweave.authority`, version 2.
- `0b5cdda6b04bc88268c20ae9fdc011c972f7f1db` — `GrassPatchRenderer::invalidateAll()` for rollback/restore of GPU batches without altering the authoritative `GrassPatch` state.
- `93d9538b26455b93a64c0ac44105e93213ada16c` — `TerrainMutationCoordinator::invalidateAllResident()` queues all currently active collision/RHI terrain tiles to rebake from canonical soil revision after save/rollback.
- `4bdfa0a243d065f8191ad700bf4a181ef2100f6c` — `TerraWeaveForestScene::setFallenTrees(vector<ForestTreeIdentity>)` is a separate stable visibility filter, independent of the game's harvested/removed tree set. Rollback from fallen to standing must NOT accidentally revive harvested trees; presentation filtering checks both.
- `2847e980abaed59f8a1d56598e6bdafba8c1d096` — NEW `engine/world/TerraWeaveNativeWorldBinding.h`. `NativeTerraWeaveWorldBinding::reconcile(const World&,WorldScene&,force)` validates ECS resource recipe and rehydrates terrain/root/grass sidecars on changed `stateRevision` (or explicit `force=true` when returning to an earlier rollback branch with the same revision). It updates near-forest root geometry, per-plant environment, and fallen-tree visibility without changing harvested state; enqueues all dynamic ground/grass GPU patches for rebuild. `publishScene(WorldScene&)` reuses `WorldSceneEarthworksBinding`, delivering an immutable near/far height snapshot only when pinned terrain replacements are ready. `groundReady()` gates actor movement on reloaded collision tiles.

## Native game registration / use

The native GameModule MUST register components/resources during `GameModule::setup` before Simulation seals the persistent registry. Example, in order:

```cpp
lib::nereid::NativeWaterPipelineConfig water;
water.enableBankErosion = true;
lib::nereid::registerNativeWaterPipeline(sim,"nereid",water);
terraweave::registerTerraWeaveBankVertexPersistence(sim);
terraweave::registerTerraWeaveAuthorityResource(sim,config);
terraweave::addTerraWeaveAuthoritativeBankSystem(
    sim,"terraweave.bank_commit",config,
    [&queue](const terraweave::EcsBankTerrainCommit& result) {
        queue.push_back(result); // host-controlled post-tick notification sink
    });
```

Create bank-cell ECS entities with `WaterBodyComponent` / `WaterContactComponent` / `WaterBankSoilComponent` / `WaterBankErosionOutputComponent` / `BankEarthVertexComponent` as appropriate (the existing NEREID native pipeline controls water sampling). Avoid duplicate bank vertices. On direct player dig action call `excavateAuthoritativeEcs` from an authorized game command handler. At the owner thread read barrier, `NativeTerraWeaveWorldBinding::reconcile(sim.world(),worldScene)` restores immutable source snapshots if revision changed. Then the native host calls `TerrainMutationCoordinator::publishVisible` with the simulation's pinned terrain tiles, `binding.publishScene(worldScene)`, `worldScene.update`, `terrain.refreshNavMesh` and Renderer3D draw. After save restore or rollback always call `reconcile(...,force=true)` to rehydrate derived caches even when the revision number equals the cached value.

**Persistence/rollback design:** `Simulation` COW world copies this resource, and `PersistentEcsRegistry` serializes it as a named schema; independent water-bank ECS components are already persisted via NEREID. No server or client writes an extra parallel authoritative terrain layer. Box3D physics pose is intentionally NOT in fixed-point deterministic ECS terrain; the server controls dynamic fallen-body simulation and replicates poses separately. A game may still opt into Stage53 standalone binary save codec and Stage54 network delta codec; do not use their local mutable terrain/water pipeline as a *second ECS writer*.

## Important safety / unfinished items

The native host's callback MUST only enqueue completed results, not touch GPU, Box3D or mutable ECS world from outside the system barrier. Resource `stateRevision` is updated by the provided excavation/erosion helpers; additional game-driven age/lighting/vegetation edits must bump it explicitly. Saving a large canonical resource each frequently changing tick has a cost; `PersistentEcsRegistry::kMaxPayload` is 32 MiB and no bandwidth/CPU benchmark is claimed. There is still no automatic install into each existing RTS/FPS client or a server network transport implementation, because their game-specific ECS setup and collision ownership must opt in. Volumetric caves/undercuts remain unsupported by heightfield patches. The native host must avoid double drawing/collision of the original static tile, and use `groundReady()` before moving actors across freshly reloaded ground.

## Actual QA

GitHub writes and latest branch readback confirmed. **NO** Clang/GCC/MSVC compilation, CMake/CTest, ECS snapshot parity, Box3D live scene, Metal/GL/Vulkan render screenshot, perf or network-replica test executed for Stage 53-55 yet. Existing TerraWeave 9/9 CPU + GPU router 1/1 successes are on earlier heads. User requested features before final tests. Keep PR #24 Draft.

**Next:** wire a concrete native project example (not the default FPS/RTS clients) with bank-spawn, dig command, save/restore, simultaneous server/client delta stream, plus controlled Box3D fallen-tree body spawning and NavMesh invalidation; then run full test suite and fix first build errors.
