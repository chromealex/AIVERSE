# AIVERSE MotionWeave — Stage 175: real coordinator-shaped adapter and GPU token-proven provider shutdown

**Date:** 2026-11-20  
**Branch:** `feature/universal-ecs-animation-pipeline`  
**Baseline:** Stage 174 head `bc804d11141ad5b78e8b7f85b1dd18e905091415`  
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/22 — do NOT merge before native acceptance  
**User preference:** finish the feature work first, run ONE coordinated native Mac CMake/C++/CTest/Metal/Vulkan/GLES/sanitizer/VRAM/crowd test pass, then fix errors. Write a distinct agent handoff after EVERY stage.

## Goals and source discovery

Stage 173 introduced `MotionWeaveScenePublication` (two-phase cosmetic World publication), Stage 174 added a dedicated live/staged `ModelAssetProvider` owner and a GPU-token-backed provider retirement queue, but native authoritative scene transitions still needed explicit choreography. The peer `feature/scene-runtime-multiworld` branch contains the actual `scene::SceneLoadCoordinator` and `SceneRuntime`. Source inspection confirmed:
- `SceneTicket.scene.value`, `SceneTicket::valid()`.
- `SceneLoadCoordinator::prepared(ticket)` (read-only `SceneAsset*` if ready), `commit(ticket)`, `cancel(ticket)`, `unload(SceneId)`, `error(ticket)`, `invalidateAfterRestore()`, `queueRestoredResidency()`.
- `scene::SceneState::scenes` is a canonical `std::map<uint64_t,SceneRecord>`; `record.phase==ScenePhase::Active` distinguishes active/persistent scenes.
- `SceneResourceAdapter::release` is invoked inside coordinator commit/unload and does **not** automatically pin a cosmetic `ModelAssetProvider`. Use a completely separately owned per-scene provider.

No blind merge or modification of the peer scene branch was attempted. This animation branch still lacks authoritative scene headers and an actual native GameModule call site. The adapter is a structurally typed bridge that invokes the REAL coordinator method names once instantiated with the peer class.

## Source changes and exact commits

1. `dba57a40e727ffd2ddbff07ff52db9fc9c01e371` — **`engine/render/ModelResourceCache.h`**:
   - Preserve public `shutdown(device)` requiring `waitForResourceIdle()`.
   - Add private `shutdownAfterCompletedSceneToken(device)` accessible ONLY to friend `ModelAssetProvider`. It requires `retired_.empty()`; no nested incomplete or failed model batch can bypass its own GPU retirement barrier.
   - Factor common cleanup into `shutdownImpl(device,completedSceneToken)`. The private token-proven path can skip a redundant full-device wait when an OUTER completed scene marker already proves all earlier resource usages finished. It still joins CPU preparation tasks and tears down resident Models and shared textures on the correct owner thread. ModelReadLease checks, GPU epoch validation, rollback and public safety checks remain in place.
2. `9c88808af1bbc3080650625fead2db9e2a728e54` — **`engine/render/ModelAssetProvider.h`**:
   - Private friend-only `shutdownAfterCompletedSceneToken(device)` for `SceneModelProviderRetirement`; joins CPU loader and binds the actual RHI epoch. If either nested image rollback or Model retirement queue has pending entries, calls existing full idle-proven public `shutdown` instead. Fast path is NEVER exposed as an arbitrary public boolean.
3. `e402eed85efca50b84263a1b106f7d085c316c3c` — **`engine/render/SceneModelProviderRetirement.h`**:
   - Check both `resourceRetirementStatus(token)==Complete` AND `resourceRetirementComplete(token)` before using completed token to release a provider. A failed/pending marker is not proof.
   - After true token completion, `shutdownOrQuarantine` uses the new private fast path. If nested queues still hold Model/image batches, the provider falls back to the original full idle barrier. Explicit shutdown keeps the original full-idle path.
   - Failed RHI or provider shutdown continues quarantining the entire owner and does not report false success; no premature GPU destruction.
4. `eef061e4987fd0d471446e42a4da8ceb95648596` — **`engine/view/MotionWeaveSceneOwnedHost.h`**: `retireUnstaged(device,Owner&)` for partially uploaded providers when a scene asset builder returns false/throws BEFORE the normal visual staging call; expose completion-approved scene shutdown diagnostics.
5. `b8ff1374cd755cf61254b0b986d7940675a954ad` — **NEW `engine/view/MotionWeaveSceneCoordinatorAdapter.h`**:
   - Header-only structurally typed `MotionWeaveSceneCoordinatorAdapter`, no peer scene header dependency; implements direct adapter calls to actual `prepared(ticket)`, `commit(ticket)`, `cancel(ticket)`, `unload(id)`, `invalidateAfterRestore()` and `queueRestoredResidency()`.
   - `commitReady(coordinator,ticket,builder,activeIds,stats,error)` stages a complete visual scene and dedicated real GPU provider **before** authoritative commit; if build fails, GPU-retires the partial provider, cancels the scene at the owner barrier; if staging fails, preserves old visual and cancels; if authoritative commit fails, discards staged provider; if commit succeeds, swaps visual Graph/World and GPU owner, then reconciles complete canonical Active IDs after Replace (retains persistent scenes).
   - Failure after authoritative success is **not** treated as permission to undo gameplay state; diagnostic error returned and native host must reconcile. No gameplay World/SceneOwner changes are performed by the MotionWeave module.
   - `cancel`, `unload`, `restoreResidencyAfterRollback` coordinate native authoritative lifetime with cosmetic GPU retirement; do not create duplicate authoritative entities on rollback residency-only restore.
6. `5d314a2c213fac48e681ccb9c276eb416700b9d2` — **`MotionWeaveSceneCoordinatorAdapter.h`**: generic `canonicalActiveSceneIds(const State*)` helper iterates peer `SceneState::scenes` in stable order, selecting only `ScenePhase::Active`. Host passes a lambda returning these IDs after commit. No dependency on peer headers is introduced.
7. `dc923e0bf170f213a01044a79a292ed114417d63` — **`SceneModelProviderRetirement.h`**: validate/bind GPU device epoch on even an unstaged failed-prewarm provider before CPU loader cancellation and retirement; stale generation quarantines rather than reaching a wrong RHI owner.
8. `8a669353159ed3f83085bad185225143eaa3abaf` — **`MotionWeaveSceneCoordinatorAdapter.h`**: `cancel` discards its staged visual/GPU provider even if coordinator cancellation throws.
9. `04eb7775479297a3f641bcc2e4bb44bde02dcbde` / `1b75bb38d5893594dd404dc0bc672d47a58c5464` — **`SceneModelProviderRetirement.h` and `MotionWeaveSceneOwnedHost.h`**: rename diagnostic to `completionProvenShutdowns` / `completionProvenProviderShutdowns` because a successful explicit GPU-idle fallback is also a valid completion proof (not only a token).
10. `12404543c71af2e77b73ad48324e8252f7ad3a9f` — **NEW `docs/motionweave-scene-coordinator-adapter.md`**: detailed owner-thread usage/host builder sketch, canonical scene state IDs, migration from peer scene branches, rollback, GPU/token/fallback rules.

## Runtime API / producer contract

```cpp
render::RenderDeviceLifetime deviceEpoch(device);
view::MotionWeaveSceneOwnedHost sceneVisuals(deviceEpoch.lease());
view::MotionWeaveSceneCoordinatorAdapter adapter(device,sceneVisuals);

if(coordinator.ready(ticket)) {
    view::MotionWeaveSceneOwnedHost::Statistics stats;
    std::string error;
    const bool result=adapter.commitReady(
        coordinator,ticket,
        [&](const scene::SceneAsset& manifest,
            view::MotionWeaveSceneOwnedHost::Owner& provider,
            std::vector<view::MotionWeaveSceneOwnedHost::Object>& objects,
            std::string& error) {
            return hostPublishCompleteScene(manifest,device,
                                            provider,objects,error);
        },
        [&]{
            return view::MotionWeaveSceneCoordinatorAdapter::
                canonicalActiveSceneIds(
                    authoritativeWorld.resourceConst<scene::SceneState>());
        },
        stats,&error);
}
```

This sample is **not a standalone compiled game**; the peer coordinator is not on the animation branch. The host's builder is responsible for creating a new dedicated `ModelAssetProvider`, publishing its parent/child Models and matching compiled Graph snapshots into the exact same cache, and using `std::to_string(SceneObjectAsset::id)` for stable cosmetic scene object identity. It must not put Model pointers in authoritative ECS or share the provider between independent scenes.

After game state load/rollback, use `adapter.restoreResidencyAfterRollback(coordinator)` at the owner barrier; it clears visual Graph/asset generations BEFORE the native coordinator releases old residency and queues residency-only manifest jobs. On authoritative unload, `adapter.unload(coordinator,id)` detaches cosmetic providers only if native unload succeeds. Poll GPU retirement at the render-owner boundary after last draw; explicit shutdown still requires correct device lifetime.

## Safety and limitations (important)

- `SceneLoadCoordinator::SceneResourceAdapter::release` can synchronously run during native `commit/unload`. It MUST NOT be the only owner of a Model/Graph used by the scene. Stage 174's dedicated provider remains in `MotionWeaveSceneOwnedHost` independently through both phases and GPU completion. Actual host builder and project-specific asset dependency rules are NOT implemented in this branch.
- **Fast GPU shutdown is conditional, not universally nonblocking.** The outer ordered scene marker proves all earlier GPU commands. ONLY when nested model-retirement and image-rollback queues are empty can the provider/cache release its live Model and textures without another global idle wait. If there are nested tokens, fallback to original `waitForResourceIdle()`. Actual `gfx::Device` backend destroy operations may still block; no latency measurements were taken.
- ModelReadLease holders and async Model worker jobs must be joined/destroyed before scene retirement; ModelResourceCache continues to reject mutations under active readers. A failed attempt conservatively quarantines provider instead of releasing GPU-live memory; it is an error/diagnostic, not success.
- No static scene header copy or blind merge; this is typed source adapter intended to compile when instantiated against the peer branch. **There is not yet a real native GameModule invocation**. Do not claim full scene integration tested.
- Shared providers across scenes, reference-counted GPU residency, full artist facial ControlRig/IK authoring and automatic morph-preserving mesh LOD remain separately open.
- Without backend token/idle proof, old providers stay pending. No implicit assumptions from frame serials, destructors or shared_ptr appearance refs.

## Actual verification vs not run

**ACTUAL:** GitHub connector read of Stage 174 source and peer branch `SceneLoadCoordinator.h`, `SceneAsset.h`, `SceneRuntime.h`; source audit of `ModelResourceCache`, `ModelAssetProvider`, `AsyncResourceRetirementQueue` and `ModelImageRollbackPool`; SHA-guarded GitHub code commits and source readback. No peer branch was modified or merged.

**NOT RUN:** native C++/CMake configure/compile/link, CTest, actual SceneCoordinator GameModule, real glTF/FBX/MotionWeave scenes, Metal/Vulkan/GLES pixels/shader compiler, ASan/TSan/UBSan, simulated failed GPU fence/idle, CPU-loader cancellation stress, VRAM/large-crowd CPU/GPU benchmarks. This is **SOURCE IMPLEMENTED, UNVERIFIED** and not ready for PR merge.

## Required native acceptance cases

1. Integration fixture with actual peer SceneLoadCoordinator: manifest Ready vs not Ready, all IDs stable, duplicate and invalid parent/child snapshots, build failure after partial GPU upload, cancel and native commit rejected. Previous authoritative and visual scenes remain valid where expected; no leaked Model destructor before fence.
2. Additive/Replace with persistent scenes; canonical `SceneState::scenes` Active ID reconciliation after successful commit. Multiple scenes can have identical local object keys but globally unique render source IDs.
3. Rollback/cold load: invalidate old coordinator tickets, reset visuals and GPU provider ownership, requeue residency-only tickets without respawning authoritative scene entities.
4. GPU marker Pending, Complete, Failed and zero-token backend, nested pending Model/cache/rollback queue vs clean provider; fast path must avoid redundant `waitForResourceIdle` ONLY in proven cases. Ensure no cache/buffer/image UAF, duplicate destruction or silent orphan success.
5. Crash/error injection: enqueue OOM, provider/CPU loader exceptions, stale RenderDeviceLifetime epoch, expired model read lease, texture-cache shared image and rollback/resident mismatches.
6. Full Mac native CMake/C++ (-Werror) and CTest, Metal/Vulkan/GLES shader and visual parity, sanitizers, memory, 1k/10k/100k actor stress, timed stalls/VRAM and GPU fence exhaustion.

## Readiness

| Subsystem | Source status | Still missing |
|---|---|---|
| Genre-neutral ECS AnimationGraph/independent children | Stage 161–164 source | native regression |
| Generic retarget, FBX/glTF morphs, 64 sparse, morph-aware LOD | Stage 165–172 source | real imported rigs, backend parity and benchmarks |
| Transactional cosmetic scene staging/Graph lifetime | Stage 173 source | native multi-scene runtime |
| Real per-scene Model provider and GPU token retirement | Stage 174 source | token/failure tests |
| **Native SceneLoadCoordinator-shaped lifecycle adapter** | **Stage 175 source implemented** | actual GameModule/peer branch merge and tests |
| **Token-complete clean-provider no-repeat-idle fast path** | **Stage 175 source implemented** | native GPU/texture fault and latency proof |
| Shared-provider reference-counted leases | NOT IMPLEMENTED | real shared cache residency design |
| Fully nonblocking shutdown with nested pending resource queues | NOT IMPLEMENTED | RHI and texture/rollback proof, perf |
| Automatic morph-preserving mesh LOD / advanced face IK/ControlRig | PARTIAL | authoring pipeline |
| Full native CMake/CTest/Metal/Vulkan/GLES/sanitizers/perf | NOT RUN | ONE coordinated Mac campaign |

## Next Stage 176

After peer scene branches are reconciled, add a concrete native GameModule adapter/call site with its actual `SceneAsset`/SceneState and resource policies (no blind merge). Close shared-provider residency and nested GPU token queue fast path or document safe remaining fallback, then finish any outstanding advanced facial/IK authoring. Finally run the agreed single full native Mac acceptance, fix errors and only then consider changing PR #22 from draft. Continue standalone handoffs after each stage.
