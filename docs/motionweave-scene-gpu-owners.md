# MotionWeave — scene-owned GPU Model lifetime (Stage 174)

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`

This is a genre-neutral native presentation lifetime contract. There are **no** writes to authoritative `SceneOwner`, `SceneState`, `Simulation` or gameplay modules.

## What the real GPU owner is

An `AppearanceAssetRegistry::Snapshot` retains material and compiled `AnimationGraph` metadata. It does **NOT** own the C++ `render::Model`, its GPU vertex buffers, morph texture, shared geometry page, shared `ModelTextureCache`, or speculative image rollback pool. Likewise, a move-only `ModelReadLease` blocks cache mutation only through a synchronous render frame; it is **not** a GPU completion fence.

A native scene therefore owns one **dedicated** `std::unique_ptr<render::ModelAssetProvider>` with all Models referenced by that scene's parent/static/independent child appearances. A coordinator/asset host must load/publish these Models into that provider BEFORE staging a scene's visual snapshot.

Stage 174 adds:

- `engine/render/SceneModelProviderRetirement.h`: an ordered `AsyncResourceRetirementQueue<std::unique_ptr<ModelAssetProvider>>`. A retired provider remains CPU/GPU owned until a real `gfx::Device` resource-retirement token is complete or an explicitly successful GPU-idle barrier proves completion. Its destructor quarantines unresolved owners instead of freeing GPU-visible storage. GPU epoch is verified through `RenderDeviceLifetime::Lease`. The queue joins outstanding CPU loader work and detaches the external file reader before enqueuing.
- `engine/view/MotionWeaveSceneOwnedHost.h`: one owner per visual scene, with a LIVE provider and an optional STAGED provider. Combines `MotionWeaveScenePublication` two-phase visual graph publication and provider retirement in the same transaction.

These are **not** a replacement for `ModelResourceCache`'s finer-grained model/texture/rollback queues. On successful GPU completion, the owner calls existing `ModelAssetProvider::shutdown(device)` which drains rollback images, models, cache-owned images and related GPU resources before destroying the provider.

## Host lifecycle

```cpp
#include "engine/view/MotionWeaveSceneOwnedHost.h"

// Device and epoch must outlive host. Host owns only cosmetic scene state.
aiverse::render::RenderDeviceLifetime deviceEpoch(device);
aiverse::view::MotionWeaveSceneOwnedHost visuals(deviceEpoch.lease());

// For a ready scene, build and publish all models in a NEW dedicated
// std::unique_ptr<ModelAssetProvider> provider, and collect complete visual
// Object inputs (with stable SceneObjectAsset::id string keys).
// Provider must already have its proper GPU lease and published assets.
// Never use a provider shared between independently retired scenes.
auto candidate = /* prepared unique_ptr<ModelAssetProvider> from host */;
std::vector<aiverse::view::MotionWeaveSceneOwnedHost::Object> objects;
aiverse::view::MotionWeaveSceneOwnedHost::Statistics stats;
std::string error;

if(visuals.stage(device,sceneId,objects,std::move(candidate),stats,&error)) {
    if(sceneCoordinator.commit(ticket)) {
        visuals.commit(device,sceneId);
        visuals.unloadAbsent(device,allCanonicalActiveSceneIds);
    } else {
        visuals.discard(device,sceneId);
    }
}

// Each owner pre-render frame:
// 1. visuals.visuals().sync(sceneId,liveProvider.resources(),dt,...)
// 2. prepareParent/prepareChild and render using move-only Prepared leases.
// 3. Destroy Prepared AFTER Renderer3D::render() submits the frame.
// 4. Collect scene GPU retirement at a device owner frame boundary.
visuals.collectRetired(device);

// After successful authoritative unload and after the last Prepared draw:
visuals.unload(device,sceneId);
// After rollback/loadState (before rehydrating scenes):
visuals.resetAfterRollback(device);
// At application/device shutdown after draw submission stops:
if(!visuals.completeForShutdown(device)) {
    // GPU completion is not proven; do NOT destroy gfx::Device or retire
    // deviceEpoch while resource queues still need that device.
}
```

The snippet is intentionally *host integration pseudocode*: it includes placeholder native load/scene coordinator variables and isn't a standalone sample app. In particular, `sceneCoordinator` exists on the separate `feature/scene-runtime-multiworld` branch, which has NOT been merged into the animation branch.

## Required order and failure semantics

- `stage` consumes ownership of the incoming provider, including on failure. If staging fails, its private World/Graph candidate is discarded **before** provider GPU retirement. Existing published scene/owner remain untouched.
- Only after an accepted authoritative commit should the host call `visuals.commit(device,sceneId)`. This first replaces the visual `Generation`, retiring old Graph/attachment source references, then moves the prior scene `ModelAssetProvider` to the ordered retirement queue.
- A rejected authoritative commit calls `discard(device,sceneId)` to retire only the new unpublished provider. A scene unload drops all cosmetic Graph/World references and then retires the scene's LIVE/STAGED providers. Replace uses the **complete active scene ID set**, preserving persistent scenes that remain authoritative.
- `resetAfterRollback` clears all presentation generations before enqueueing old providers. New World/Model epoch and stable scene IDs must be republished from the restored authoritative state; never restore borrowed Graph/Entity/GPU pointers from a save.
- `retire` must occur on the real device owner AFTER animation workers joined, previous frame-scoped CPU `ModelReadLease` packets were released, and GPU draw command submission was completed. The queue performs its GPU token polling only after submission, not inside a render encoder.
- `ModelAssetProvider::shutdown` currently calls `gfx::Device::waitForResourceIdle()`. **Even if the ordered token has already completed, final provider shutdown can still stall the GPU.** Stage 174 implements SAFE owner lifetime, not yet a fully nonblocking GPU destruction path. A future backend-specific `shutdownAfterCompletionProof` would need a separately audited, enforceable token/fence/texture-cache/rollback contract.
- If a token is unsupported, the queue holds providers pending until a bounded pressure threshold (128 owners) permits a proven GPU-idle fallback, or an explicit shutdown drain. If a token fails, the underlying queue retains it until successful full-idle recovery. An allocation/shutdown/epoch failure is diagnosed and conservatively quarantines the provider rather than risking GPU use-after-free.
- An orphan is an intentional leak and is **reported as failure**, not successful shutdown. The native host must not quietly destroy the Device on a failed completion or orphan count.
- This wrapper assumes **dedicated ModelAssetProvider ownership per scene**. If multiple scenes share a single provider, first implement a reference-counted scene-residency lease on that real provider with explicit final GPU-retirement semantics; wrapping arbitrary `shared_ptr<AppearanceAsset>` is not enough.

## Gaps for final integration and acceptance

- The real `SceneLoadCoordinator` is owned by another agent/branch. No in-game `GameModule` integration is yet committed; adapt only after reviewing the other branch and preserving its existing authoritative resource adapters.
- Full nonblocking provider teardown and per-texture read/share lifetime across multiple providers remains open. Existing `ModelResourceCache` async retirement is already token-backed, but whole-provider shutdown still performs a full idle proof.
- Test two active scenes and separate providers, successful/failed staged commit, Replace with persistent scene, unload, rollback and full device shutdown.
- Inject pending/completed/failed/unavailable GPU tokens and failed `waitForResourceIdle`, CPU job cancellation and texture/morph upload rollback; confirm no provider disappears before real completion.
- Verify GPU thread/epoch changes, allocator failure at queue enqueue, active read leases blocking provider shutdown, pending provider counts, orphan status and no false green signals.
- Full native Mac CMake/CTest/Metal/Vulkan/GLES/ASan/TSan, GPU fault and crowd tests have **not** been run in this chat.

See `docs/agent-handoffs/animation-stage174-scene-provider-gpu-residency-2026-11-20.md` for exact source commit IDs and next steps.


## Stage 175 — completed-token fast shutdown and canonical coordinator adapter

The public `ModelAssetProvider::shutdown()` remains unchanged and requires true GPU idle. The scene retirement queue now has a **private** fast path that avoids another global idle call only when an ordered `gfx::Device` completion token has been confirmed and both nested Model and image-rollback retirement queues are empty. If either is pending, the old full-idle fallback is used. The outer token verification also requires `resourceRetirementStatus(token)==Complete`; pending/failed tokens do not authorize release. This matches the resource-retirement semantics in Metal's ordered command queue, GL/GLES sync fence and Vulkan's queue submission fence, but actual runtime proof and measurements remain pending.

New cross-branch coordinator adapter: `engine/view/MotionWeaveSceneCoordinatorAdapter.h`; see [Stage 175 usage guide](motionweave-scene-coordinator-adapter.md) and [handoff](agent-handoffs/animation-stage175-scene-coordinator-gpu-token-fastpath-2026-11-20.md). It uses the real peer SceneLoadCoordinator methods with template binding when the native project has those headers. This Stage 175 implementation still does NOT include a game-specific call site or a merge of the peer scene branch. The host owns the complete scene visual asset builder and its canonical SceneState reader.
