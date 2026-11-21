# AIVERSE MotionWeave — Stage 174: GPU-safe real scene Model provider residency

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Baseline: Stage 173 head `04f0c1c40fd8a8880db796d012b8f5b5051d07e8`
PR: https://github.com/chromealex/AIVERSE/pull/22 — keep DRAFT/UNMERGED
User contract: animation must be genre-independent, and every implementation stage must leave an independent handoff with what changed/API/tests actually run/risks/next step. Finish source features before ONE coordinated Mac CMake/CTest/Metal/Vulkan/GLES/sanitizer/GPU lifetime/perf run. DO NOT claim tests passed without logs.

## Problem closed in source

Previously `engine/view/MotionWeaveScenePublication.h` (Stage 173) owned ONLY the cosmetic World/attachments and immutable `AppearanceAssetRegistry::Snapshot` objects. They retain compiled Graph/appearance metadata but do not own the borrowed `Model*`, its GPU buffers, morph textures, `ModelTextureCache`, shared geometry pages or speculative image rollback pool. The peer `SceneLoadCoordinator` can release scene resources on commit/unload. Merely deleting the old presentation world does not protect GPU commands already submitted in the previous frame.

Stage 174 implements a dedicated, genre-neutral native scene owner pairing **each cosmetic scene generation with the actual unique `ModelAssetProvider` owning all its Models**, and a GPU token-backed *whole-provider* retirement queue. It does not inject anything into the authoritative World, nor attempt to replace the cache's existing fine-grained token queues.

## Actual source changes and commits

1. `0c56b2196a66b2af7b25f74c5931df4a20457be9` — NEW `engine/render/SceneModelProviderRetirement.h`:
   - `SceneModelProviderRetirement` owns `AsyncResourceRetirementQueue<std::unique_ptr<ModelAssetProvider>>`. Queue resources are the REAL Model owners, so cached Models/Graph-borrowed CPU structures, shared textures, image rollback pool and their GPU handles stay alive until a real ordered `gfx::Device::beginResourceRetirement` token completes or an explicit successful GPU-idle barrier proves completion.
   - Must retire only after all Scene/Graph/Prepared CPU references detach, workers join and draw submissions finish. `collect` polls tokens on the GPU owner thread; backends with no tokens preserve owners until 128 pending or shutdown full-idle barrier. Successful token cleanup calls existing `ModelAssetProvider::shutdown(device)`.
   - Model provider owners cannot be destroyed at queue destructor when status is unresolved: quarantine the queue on host destruction. On enqueue allocation failure, try explicit idle fallback; if no proof, intentionally orphan rather than risk GPU use-after-free. Expose failure/orphan statistics.
2. `3b6481908c99d434ae779a0c6c27e47a234b5503` — NEW `engine/view/MotionWeaveSceneOwnedHost.h`:
   - `stage(gfx::Device&, sceneId,objects,unique_ptr<ModelAssetProvider>,stats,error)` fully consumes a uniquely owned, dedicated per-scene provider on success OR failure; an unpublished failed candidate is retired via queue after candidate Graph/World detach. It uses Stage 173's private visual `stageScene`.
   - `commit(device,sceneId)` only after an accepted authoritative scene commit. First swaps and destroys old presentation Graph/attachment generation, then retires its old REAL Model provider while replacing the live provider with the staged one.
   - `discard` retires only an abandoned staged candidate (old live scene unchanged); `unload`, `unloadAbsent` (with complete canonical active-scene set), `resetAfterRollback`, `collectRetired`, `completeForShutdown` finish other lifecycle routes. All owners remain render-thread-only, no simulation ECS code.
   - Destructor fails closed: if the app skipped safe explicit shutdown, clear cosmetic Graph Worlds and conservatively orphan remaining provider owners rather than destruct GPU-live C++ Models. Read-only pending/orphan/failure diagnostics are available.
3. `85de238d3512379a678a70087e223b164d0e2b80` — `SceneModelProviderRetirement.h`: a quarantined provider is NOT reported as successful GPU retirement/shutdown even if the queue itself becomes empty.
4. `c5013fb5387533f6451521fa8a5c622b9a81da02` — `SceneModelProviderRetirement.h`: expose `validDevice` from real `RenderDeviceLifetime::Lease`.
5. `4caf4ba5d2685cb036db687946bcfbeb11e3f724` — `MotionWeaveSceneOwnedHost.h`: fail scene stage/commit if device epoch changed, rather than publishing stale CPU/GPU Models.
6. `5730fcc290424145c4a7226790a3a5288f314285` — `SceneModelProviderRetirement.h`: `ModelAssetProvider::setReader({})` joins outstanding async CPU model loads and releases the borrowed host project reader before the provider enters GPU-fence retirement. A failure quarantines provider rather than destroying loader/Models prematurely.
7. `22011208866c21705681976998a390ee00810e1d` — `MotionWeaveSceneOwnedHost.h`: on staging, bind the *same* `RenderDeviceLifetime::Lease` to the candidate `ModelAssetProvider` and its `ModelResourceCache`. This prevents accepting a pre-imported model provider from a stale device generation even if its gfx Device address was reused.
8. `0f32f40b3ff935bb84a211e3b60263063b975e9d` — NEW `docs/motionweave-scene-gpu-owners.md`: host API, transaction ordering, GPU token semantics, requirements and failure cases.

9. `d2e31ef8769607528de188b340372f8eff208058` — `MotionWeaveSceneOwnedHost.h`: expose `cpuLoaderShutdownFailures()`, `providerEnqueueFailures()`, `providerPollFailures()` alongside orphan/pending/shutdown diagnostics. An agent can now report native scene provider GPU/CPU failure status without inspecting queue internals.

## Implementation/API

```cpp
// Device and epoch are host-owned. Epoch must outlive the scene host.
aiverse::render::RenderDeviceLifetime epoch(device);
aiverse::view::MotionWeaveSceneOwnedHost visuals(epoch.lease());

// Host loads COMPLETE scene asset Models in a dedicated, unique provider.
// It publishes immutable parent/static/animated child appearances from
// that SAME cache before calling stage().
std::unique_ptr<aiverse::render::ModelAssetProvider> provider=...;
std::vector<aiverse::view::MotionWeaveSceneOwnedHost::Object> objects=...;
aiverse::view::MotionWeaveSceneOwnedHost::Statistics stats;
std::string error;
if(visuals.stage(device,sceneId,objects,std::move(provider),stats,&error)) {
    if(sceneCoordinator.commit(ticket)) {
        visuals.commit(device,sceneId);
        visuals.unloadAbsent(device,completeActiveSceneIds);
    } else {
        visuals.discard(device,sceneId);
    }
}
// After draw submission and destruction of all CPU Prepared packets:
visuals.collectRetired(device);
// Successful authoritative unload / rollback:
visuals.unload(device,sceneId);
visuals.resetAfterRollback(device);
// Explicit shutdown before destroying RHI:
if(!visuals.completeForShutdown(device)) {
    // GPU retirement proof failed or an owner was orphaned:
    // DO NOT label shutdown successful or destroy the live Device yet.
}
```

The code fragment uses placeholder scene coordinator variables and is not a standalone compiled sample. The real coordinator lives in `feature/scene-runtime-multiworld` and was inspected in Stage 173; there was NO blind branch merge or production GameModule adapter in this stage. See `docs/motionweave-scene-gpu-owners.md` for ordering and more details.

## Guarantees and constraints

- ONE native provider per scene. A Model snapshot must reference `incoming->resources()` or publication's cache read validation fails. Providers must not be shared directly between independently retired scenes; a shared provider requires a separate reference-counted real-owner lease.
- Scene/provider retirement is ordered: **destroy old cosmetic World/Graph players before releasing Model/Graph CPU storage**, then queue actual providers until GPU completion and shutdown. No shared_ptr to a mere appearance is mistaken for GPU ownership.
- Aborted stage leaves previously published scene and provider; old scene cannot be reclaimed by a failed new import.
- Owner cannot stage/commit across changed `RenderDeviceLifetime` epoch. GPU queue does not infer completion from frame count or destructor calls.
- On no token, fallback can wait for full GPU idle under pressure; explicit shutdown always must prove idle. OOM, invalid epoch, CPU queue failure, unsuccessful GPU wait or shutdown error preserves potentially in-flight Model handles as an intentional leak (orphan) and increments diagnostics. **Intentional orphaning is an error, not success.**
- **NOT FULLY NONBLOCKING:** `ModelAssetProvider::shutdown` itself calls full `waitForResourceIdle()` even after a completed per-provider retirement token. This can stall GPU. Stage 174 delivers safe whole-provider ownership but does not implement an alternate token-proven nonblocking cache/texture teardown or assert 0 ms waits.
- Existing ModelResourceCache per-model retirement, shared ModelTextureCache, geometry pool and rollback pool still require native fault tests. A visual host must stop creating new frames, release ModelReadLeases, and stop parent Animator/Graph readers before calling unload/commit and must retain RHI Device through completeForShutdown.
- No integrated `SceneLoadCoordinator` GameModule event callbacks or direct authoritative Scene World linking; sibling scene branches were not merged. The peer coordinator's `SceneResourceAdapter::release` must not bypass this owner and prematurely destroy the scene Model provider.
- No performance, Metal, Vulkan or GLES pixel parity claims.

## What was actually verified

**ACTUAL:** connected GitHub source audit of `ModelAssetProvider.h`, `ModelResourceCache.h` (invalidation, read leases, async token queue, shutdown), `AsyncResourceRetirementQueue.h`, `NativePresentationRetirement.h`, `RenderDeviceLifetime.h`, `ModelImageUploadTransaction.h`, `ModelStagingTransaction.h`, `ModelTextureCache.h`; SHA-guarded remote source commits, readback of new helper classes and contracts.

**NOT RUN:** CMake/configuration, C++ native compiler/link with -Werror, CTest, scene host runtime, true per-device GPU tokens/fences, Metal/Vulkan/GLES shaders/pixels, ASan/TSan/UBSan, VRAM/crowd benchmarks, fault-injected shutdown, and CI. All implementation claims are **SOURCE ONLY, UNVERIFIED**. The code is not yet validated for production integration or safe merge.

## Acceptance matrix (final Mac pass)

1. Two dedicated scene providers, scene 1 live while scene 2 stages/fails; previous appearance/Model/Graph and GPU owner stay valid.
2. Successful staged authoritative commit: old Graph owner clears, prior provider enters queue; new provider stays until future unload; no stale read scope or double Animator tick.
3. Cancelled/failed staged commit: only candidate provider retires; old scene remains visible and valid. Persistent Replace keeps correct providers.
4. GPU token Pending/Complete/Failed and no-token device: old owner remains until true proof; failed fences never free resources; fallback at pressure and full-shutdown barrier.
5. GPU idle failed, provider shutdown throws, CPU callback/loader shutdown throws, enqueue OOM, render-device epoch changed: assert orphan count / safe failure and no accidental Model destruct.
6. Multiple Model/Graph read leases live at retire: actual ModelResourceCache shutdown prevents premature destruction and records failure; release lease on owner thread and retry behavior requires future design.
7. Shared model texture promotion and rollback image pool under staged rejection; guarantee no double GPU texture destruction.
8. Native C++ compile/CTest, modelview/editor visual scenes, Metal/Vulkan/GLES parity, ASan/TSan/UBSan, 1k/10k scene/actor stress, RSS/VRAM/idle-stall measurement.

## Readiness

| Area | Current source status | Missing |
|---|---|---|
| Universal MotionWeave AnimationGraph/ECS | Incrementally implemented | Native CMake/CTest |
| Independent child Graphs, hot-reimport, 64 morph, authored visual LOD | Stage 161–172 source | Real assets/pixels/perf |
| Transactional cosmetic scene publication | Stage 173 source | Actual integrated GameModule |
| **Whole dedicated scene Model provider retention** | **Stage 174 source implemented** | Native tests and real integration |
| **Ordered GPU token retirement of scene provider** | **Stage 174 source implemented** | Backend fault tests, reduce blocking shutdown |
| CPU async model reader shutdown and RHI epoch validation | Stage 174 source | Real multi-thread/device regression |
| True shared-provider reference-counted scene leases | NOT IMPLEMENTED | Real shared Model provider owners |
| Token-only NONBLOCKING provider/texture/rollback teardown | NOT IMPLEMENTED | Audit whole GPU owner dependency graph |
| Direct SceneLoadCoordinator event hooks | NOT IMPLEMENTED | Reviewed cross-branch integration |
| Full facial Control Rig/IK/automatic morph LOD | PARTIAL | Artist workflow and deformable mesh simplifier |
| Full Mac CMake/CTest/Metal/Vulkan/GLES/sanitizers/crowd/perf | NOT RUN | One coordinated native campaign |

## Next Stage 175

Implement the real SceneLoadCoordinator integration after safely reconciling peer scene branches, with a first-class resource residency owner/defer-release policy. Review and (where safe) implement a token-completion-proven nonblocking provider shutdown that covers every Model, ModelTextureCache resident, shared geometry and image rollback handle, **without** weakening failure handling. Continue per-stage handoffs and readiness tables, then perform the user-requested single native Mac acceptance run before PR #22 merge.
