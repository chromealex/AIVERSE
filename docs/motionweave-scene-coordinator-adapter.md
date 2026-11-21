# MotionWeave — scene coordinator integration (Stage 175)

Status: **source implemented on** `feature/universal-ecs-animation-pipeline`; **not** tested by native Mac C++/CTest/GPU. The authoritative scene coordinator belongs to peer branch `feature/scene-runtime-multiworld` and is NOT present in this animation branch yet. No merge of peer branches occurred.

## Real peer API and the integration boundary

The actual `scene::SceneLoadCoordinator` in `feature/scene-runtime-multiworld` exposes `request(asset,LoadMode,persistent)`, `poll()`, `prepared(SceneTicket)`, `commit(SceneTicket)`, `cancel(SceneTicket)`, `unload(SceneId)`, `invalidateAfterRestore()`, `queueRestoredResidency()`, and `error(SceneTicket)`. It receives the authoritative `World` and native `SpawnPrefab` callback. The View/Animation pipeline must not mutate that World or install presentation Graph clocks into its entities.

The Stage 175 adapter `engine/view/MotionWeaveSceneCoordinatorAdapter.h` is a header-only **structurally typed** bridge. It can compile in the animation branch without importing the absent peer scene headers, but calls the coordinator's **real method names** once instantiated in a native GameModule where the peer branch has been reconciled. It doesn't fake `SceneOwner`, `SceneTicket`, `SceneAsset` or `SceneState` definitions.

It pairs the Stage 174 `MotionWeaveSceneOwnedHost` (one dedicated `ModelAssetProvider` per scene) with the authoritative commit lifecycle.

## Owner-thread usage sketch

```cpp
// This is integration pseudocode; peer branch and host assets are required.
#include "engine/view/MotionWeaveSceneCoordinatorAdapter.h"
#include "engine/scene/SceneLoadCoordinator.h" // after peer branch merge

render::RenderDeviceLifetime gpuEpoch(device);
view::MotionWeaveSceneOwnedHost visuals(gpuEpoch.lease());
view::MotionWeaveSceneCoordinatorAdapter adapter(device,visuals);

// Start/poll/await readiness using native scene coordinator API.
// At the deterministic structural barrier (never in an asset worker):
const auto ticket=coordinator.request("scenes/lobby.json");
coordinator.poll();
if(coordinator.ready(ticket)) {
    view::MotionWeaveSceneOwnedHost::Statistics stats;
    std::string error;
    const bool ok=adapter.commitReady(
        coordinator,ticket,
        [&](const scene::SceneAsset& manifest,
            view::MotionWeaveSceneOwnedHost::Owner& provider,
            std::vector<view::MotionWeaveSceneOwnedHost::Object>& objects,
            std::string& reason) -> bool {
            // Create ONE unique dedicated ModelAssetProvider. All parent,
            // static children and independently animated child Models,
            // Graphs and immutable snapshots MUST refer to THIS cache.
            // For every manifest object use std::to_string(object.id) as
            // its stable cosmetic key, not Entity.index.
            return buildPublishedVisualScene(manifest,device,
                                             provider,objects,reason);
        },
        [&]() {
            const auto* state=simulation.world()
                .resourceConst<scene::SceneState>();
            return view::MotionWeaveSceneCoordinatorAdapter::
                canonicalActiveSceneIds(state);
        },
        stats,&error);
}

// Authoritative unload accepted first, then cosmetic graph disconnect and
// token-safe retirement of scene's actual ModelAssetProvider:
adapter.unload(coordinator,scene::SceneId{mySceneId});

// AFTER Simulation::loadState() and before resuming authoritative ticks:
const auto residencyTickets=adapter.restoreResidencyAfterRollback(coordinator);
// Await each residency ticket and call commitReady() for it. The peer
// coordinator's residency-only commit does NOT recreate gameplay entities.

// Each render frame on the device-owner thread:
visuals.collectRetired(device);

// Final shutdown (after last draw/Prepared lease and without new commands):
const bool shutDownSafe=visuals.completeForShutdown(device);
if(!shutDownSafe) {
    // Do not destroy gfx::Device or retire gpuEpoch. Fix GPU lifetime issue.
}
```

Variables `simulation`, `buildPublishedVisualScene`, `device`, `coordinator`, `mySceneId` are host-specific placeholders. The snippet is NOT a tested in-repository GameModule. It illustrates the real API bindings and the required order.

## Atomicity and failure rules

- `commitReady` only acts on a **Ready** ticket with a valid immutable prepared manifest. Its builder must be CPU/GPU owner-thread safe and provide a unique provider and complete visual objects. If the builder fails or throws after allocating Models/textures, `retireUnstaged` retains that partial provider through an ordered GPU completion queue; it is NEVER destroyed by local unique_ptr unwinding.
- `MotionWeaveSceneOwnedHost::stage` uses a private, presentation-only World and validates parent/child Model cache residency and Graph/source epochs. A failure leaves an existing live visual scene untouched and retires only the failed candidate.
- The adapter calls real `coordinator.commit(ticket)` only after successful visual staging, then calls visual host `commit(device,sceneId)`; failure calls `discard`. After success it derives complete ACTIVE scene IDs from the authoritative `SceneState` and reconciles old visual scenes, preserving persistent scenes.
- If a failure/exception occurs **after** the coordinator has committed gameplay, do NOT undo authoritative scene state or prematurely discard the new scene. Report an explicit integration error and reconcile at the next owner barrier.
- A ready/preparing ticket cancelled through `adapter.cancel` discards staged cosmetic provider even if the coordinator's cancel call throws. `adapter.unload` only drops GPU scene ownership if native coordinator unload succeeded.
- `adapter.restoreResidencyAfterRollback` clears all presentation snapshots and providers first, invalidates native coordinator epochs, and returns its new residency tickets. Only pointer-free scene state survives a rollback.
- `SceneResourceAdapter::release` may run during native authoritative `commit/unload`. It must **not** own/destroy the dedicated `ModelAssetProvider` controlled by the visual host. Native dependency refcounts and cosmetic GPU owner lifetimes are two separate mechanisms.
- `SceneModelProviderRetirement` validates the RHI generation and joins all provider CPU loaders before queueing; token failures cannot free in-flight Models. A provider whose token cannot be proven complete remains pending or is quarantined as an explicitly reported orphan.

## Stage 175 fast GPU-shutdown path

The existing public `ModelAssetProvider::shutdown(device)` keeps its conservative `waitForResourceIdle()` semantics.

A **private** path accessible ONLY to `SceneModelProviderRetirement` has been added:
- `ModelAssetProvider::shutdownAfterCompletedSceneToken`
- `ModelResourceCache::shutdownAfterCompletedSceneToken`

When the enclosing scene owner is retired after a verified completed ordered GPU token (or positive full-idle fallback), the provider can skip a **second device-wide wait** provided BOTH its nested model retirement queue and its image rollback queue have no outstanding entries. In that case models and texture cache can be destroyed on the owner thread using the completed scene token as prior-submission proof. The normal `shutdown` still handles nested pending queues and requires GPU-idle. Failed markers are not completion proof. This avoids unnecessary stalls for clean providers but is NOT a blanket claim of fully nonblocking shader/resource teardown on all backends.

The proof is encapsulated within the retirement queue, not passed as user-forgeable booleans to public cache APIs. The queue checks `resourceRetirementStatus(token)==Complete` and `resourceRetirementComplete(token)` before releasing any owner.

## Still pending

- **Actual native GameModule integration:** the peer `SceneLoadCoordinator` headers/callbacks remain on other branches; no cross-branch merge or app call site was added. A native host must provide its real visual builder and canonical `SceneState` callback.
- **Shared-provider residency:** the wrapper requires one dedicated provider per logical scene. If scenes share a cache, add a real reference-counted provider residency lease rather than `shared_ptr<AppearanceAsset>`.
- **Full GPU teardown optimization:** nested pending model/texture/rollback token queues still require full-idle fallback; explicit device shutdown and some backend destroy functions may wait. Do not report zero stalls without RHI measurements.
- **Validation:** native Mac CMake/C++/CTest, SceneCoordinator integrated startup/Replace/rollback, Metal/Vulkan/GLES pixels, sanitizer/fault injection/VRAM/crowd profiles have NOT been run in this chat.

See [Stage 175 agent handoff](agent-handoffs/animation-stage175-scene-coordinator-gpu-token-fastpath-2026-11-20.md).
