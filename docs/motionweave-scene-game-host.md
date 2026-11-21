# MotionWeave concrete native scene GameHost — Stage 176

Date: 2026-11-20. Branch: feature/universal-ecs-animation-pipeline.
Status: source authored, native compilation and execution deferred.

## Source ownership

The canonical SceneAsset.h, SceneRuntime.h and SceneLoadCoordinator.h have been copied verbatim from main into this feature branch at the source blob SHAs listed in the Stage 176 handoff. This was an intentionally narrow sync, not a full merge of unrelated engine work.

**engine/view/MotionWeaveSceneGameHost.h** is a concrete presentation host bound to AIVERSE Simulation, SceneLoadCoordinator, SceneState, gfx::Device and a RenderDeviceLifetime lease. It exposes scene request/poll/readiness, staged authoritative commit, cancellation, unload, rollback residency restore, independent child AnimationGraph frame sync, preparation of parent/child draw packets and GPU retirement. It reads canonical ACTIVE scene IDs from Simulation::world() to retain persistent scenes correctly across Replace.

**engine/view/MotionWeaveSceneAssetBuilder.h** converts real SceneAsset objects to visual objects, not anonymous View entities. It creates a dedicated ModelAssetProvider for the scene generation and calls a project-specific visual prefab resolver for each SceneObjectAsset. The stable object key is always the decimal SceneObjectAsset::id, never Entity.index. A prefab without visual output can return an empty optional. Every emitted appearance Model must be resident in the SAME provider, checked by ModelReadLease. On failure, the partial provider remains in the builder output for GPU-safe retirement by the scene adapter.

## Native project integration

The native GameModule/engine host owns Simulation, SceneLoadCoordinator, gfx::Device and its RenderDeviceLifetime. Construct MotionWeaveSceneGameHost on the graphics/scene owner thread, passing those real instances and a current device lease. Construct MotionWeaveSceneAssetBuilder with the same device/lease, a Model file reader, and a visual resolver callback.

The resolver receives (const scene::SceneObjectAsset&, render::ModelAssetProvider&, gfx::Device&, optional<MotionWeaveSceneGameHost::Object>&, string& error). It chooses visual assets using the prefab and properties, calls provider.load() to publish all needed Models and publishes immutable parent/static child/independent animated child Graph snapshots in that provider. Do not mutate authoritative gameplay state or use a second presentation Graph writer here.

Call the GameHost request() and poll() as normal; only call commitReady(ticket,builder,stats,error) for a Ready SceneTicket **at a deterministic structural barrier** after all peers/host systems are ready. The GameHost stages the cosmetic World/Graph and GPU provider, then calls the REAL coordinator.commit() and reconciles canonical ACTIVE SceneState IDs. A failure does not replace a valid previous scene; partial GPU owners are retired behind true GPU completion.

Once per presentation frame, call GameHost sync(sceneId,dt,stats,error) and then prepareParent()/prepareChild() for already published objects. Parent pose is supplied by the one host animation owner. Retain all move-only Prepared packets until synchronous Renderer3D::render() completes. Run collectRetired() on the render owner after submission.

On authoritative unload call GameHost unload(). Following Simulation::loadState, call restoreResidencyAfterRollback() before resuming ticks, then rehydrate residency-only scene jobs without respawning authoritative Entities. At application shutdown, call completeForShutdown() and do not destroy the gfx::Device while that completion fails. A clean, completed scene GPU marker can skip a SECOND device-wide idle wait; nested pending/failed resources retain the conservative full idle fallback.

## Project-specific integration remains

The framework cannot invent a game's prefab-to-Model/Graph asset policy, so the actual project must supply a resolver and instantiate this host in its own bootstrap. Do not put RTS, FPS or any genre dependencies in the animation core.

The original `commitReady()` still uses a dedicated provider per scene. Stage 177 adds **opt-in real-owner shared residency** via `commitReadyShared()` and `SceneModelProviderResidency`, with reference-counted leases to the actual ModelAssetProvider. This is not AppearanceAsset pointer sharing. The resident cache is treated as immutable while shared; the second/additive scene must use the read-only `MotionWeaveSceneAssetBuilder::buildResident()` resolver and an identical, versioned residency key. See [shared GPU scene residency](motionweave-shared-scene-gpu-residency.md). Shared residency across different hosts/RHI epochs, live cache mutation and native project-specific bootstrap remain outside this stage.

## Deferred compilation and validation

tests/animation/scene_game_host_api.cpp is registered as the CTest target animation_scene_game_host_api. It includes the concrete types, forces a typed commitReady instantiation, and checks ACTIVE versus Loading SceneState filtering. The fixture is **committed, not compiled or run** in this stage.

Full CMake/CTest, runtime scene load/replacement/rollback, Metal/Vulkan/GLES, ASan/TSan, GPU failed-fence and VRAM/crowd benchmarks remain scheduled for the user's one coordinated native Mac validation after source feature completion.

See docs/agent-handoffs/animation-stage176-concrete-scene-game-host-2026-11-20.md for exact commits and next steps.
