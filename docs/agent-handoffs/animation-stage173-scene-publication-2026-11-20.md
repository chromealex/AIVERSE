# AIVERSE MotionWeave — Stage 173: transactional cosmetic scene publication bridge

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Starting commit: `8a19b29acd9020a20b51251e876b34b6a8d6a415` (Stage 172)
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — verified OPEN / DRAFT / UNMERGED
Audience: next implementation agent / native Mac verifier
User contract: genre-independent animation; standalone handoff at EVERY stage with actual source/files/commits/API/tests/limitations/next step. Finish source features first, then ONE full native Mac C++/CTest/Metal/Vulkan/GLES/sanitizer/VRAM/crowd validation; do not claim tests ran.

## Why Stage 173 exists

Stages 159–172 contained a genre-neutral `SceneAttachmentHost` and `AnimatedSceneAttachmentSources`, but no transaction accepting multiple published scene objects as an all-or-nothing cosmetic generation. The authoritative `engine/scene/SceneLoadCoordinator.h` and `SceneRuntime.h` are in **peer** branches `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration`, NOT this feature branch. Source audit confirmed `SceneLoadCoordinator::SpawnPrefab(World&,Entity,const SceneObjectAsset&,string&)`, `request/poll/prepared/commit/unload`, `SceneState::scenes` with persistent records, and `afterRollback()/queueRestoredResidency()`. Its spawning callback receives the authoritative World, which MUST NOT own cosmetic Model/Graph pointers, presentation Animator states or GPU lifetime.

Stage 173 implements a source-neutral presentation bridge **without merging peer branches or modifying authoritative ECS state**. Direct GameModule integration is a separate step after reviewed scene-branch reconciliation.

## Delivered source and commits

1. `8c5da580c07065b199d78d19a93b85ea8f20b698` — new `engine/view/MotionWeaveScenePublication.h`:
   - Public `MotionWeaveScenePublication::Object` supplies stable scene object key, immutable `AttachmentLayout`, parent Appearance snapshot, a caller-owned static child resolver and independently animated child Appearance snapshots by stable authored child ID.
   - A private `Generation` per logical scene owns a presentation-only `World`, `SceneAttachmentHost`, ONE `AnimatedSceneAttachmentSources` Graph scheduler, snapshot metadata and stable object mappings. No authoritative World/SceneOwner pointer or game-genre modules.
   - A complete candidate generation is built separately. Every layout, parent publication, stable ID, independent child graph/model match, named socket, static child resolution, CPU cache read lease and Graph initial pose is validated before making a new visible generation. A failure leaves last-good scene/players intact.
   - `prepareParent` emits the host-precomputed parent packet; `prepareChild` emits Graphless child packets based on one independent presentation evaluator. `sync(scene,cache,dt,...)` performs one live Graph step at owner barrier. A move-only parent/child Prepared must stay alive through synchronous `Renderer3D::render`.
2. `51bfc416151d47ec965da61e7487199811598829` — append `unloadAbsent(activeSceneIds)` for successful authoritative Replace and `resetAfterRollback()` for invalidation of old visual worlds, with preflight rejecting duplicate/zero scene IDs. Persistent old scenes remain when still in the canonical active set. `unload(sceneId)` clears Graph owners before visual ECS child publication is destroyed.
3. `d2920300595002f509cb0eeea502574487a918db` — allocate never-reused global per-host draw identities for BOTH parent and independently animated child packets; independent per-scene Graph hosts can otherwise start source IDs at 1 and collide if sharing a render-only `View`. `prepareParent/prepareChild` apply these identities only after successful packet preparation.
4. `435937f13efea225a00945f3e908331ee7a19730` — split publication into `stageScene/commitStaged/discardStaged` for two-phase host coordination. `stageScene` owns a fully prepared private World in `pending_`; `commitStaged` moves it into a preallocated scene map slot after an accepted authoritative commit; failed authoritative commit discards it. Empty preallocated slots are not counted as active scenes. `replaceScene` remains a convenience staging+commit API for standalone authoring hosts. Staging errors invalidate an older pending candidate for the same scene but do not alter the previous active visual generation. Allow missing static resolver only when the authored layout has NO enabled static children.
5. `e165d51efa95ba2694c2093dbf7897891f4aea4a` — make `unload/sceneCount/unloadAbsent` distinguish an actually published visual scene from a preallocated but uncommitted empty slot.
6. `dbaa55217acfa3e07145fa5fa0f2cd471088caf2` — explicitly materialize optional `Resolver` as a `std::function`, avoiding reliance on conditional-expression resolution between function and lambda types.
7. `004cb1a912b6359da20f0bc75dd4b7318ada6b9e` — new public workflow and resource-lifetime guide `docs/motionweave-scene-publication.md` with staged authoritative commit, rollback, scene Replace and parent/child render usage, plus backend acceptance plan.

## Public API contract

```cpp
using Publisher=aiverse::view::MotionWeaveScenePublication;
Publisher visuals;
std::vector<Publisher::Object> visualObjects;
// Host resolves SceneAsset::objects stable IDs, authored AttachmentLayout,
// native appearance snapshots and static resolvers; NO raw authoritative Entity.
Publisher::Statistics stats;
std::string error;
if(!visuals.stageScene(sceneId,visualObjects,modelProvider.resources(),
                       stats,&error)) {
    // keep last good scene, decline authoritative commit
}
if(sceneCoordinator.commit(ticket)) {
    visuals.commitStaged(sceneId);
    // reconcile with complete ACTIVE SceneState IDs after Replace
    visuals.unloadAbsent(activeSceneIds);
} else {
    visuals.discardStaged(sceneId);
}
// On native unload barrier: visuals.unload(sceneId);
// On rollback/loadState barrier: visuals.resetAfterRollback();
// On live frame: visuals.sync(sceneId,cache,dt,stats,&error);
// On render: visuals.prepareParent(...), visuals.prepareChild(...);
// All Prepared packets must outlive Renderer3D::render().
```

**IMPORTANT:** This is host integration pseudocode, not a compiled in-repository invocation of peer coordinator. Asset upload/residency must be staged independently by the host BEFORE `stageScene()` when the peer coordinator's own resource acquisition is not yet committed. Do not call any of these from the asynchronous scene-file load worker. Caller retains OLD and NEW immutable Model/Graph/GPU providers across visual staging, authoritative commit, old visual detach and proven GPU completion.

## Ownership/safety details

- Graph player per independent child only; static socket children share parent pose. Parent animation clock remains host-owned. Candidate Graph initial preflight runs with dt=0 inside an unpublished World. No second LIVE Graph writer and no gameplay World mutation.
- Stable scene key is `SceneObjectAsset::id` as an external string, not `Entity::index`. Source and child ECS handles are reallocated per published generation; never serialize them. Parent/child `View::Object.id` is a monotonically unique draw ID allocated by this visual host.
- Existing scene remains visible until all candidate objects, static socket plans and child Graphs preflight successfully. `saveScene` emits authored, pointer-free JSON only. Graph playback handoff is portable named state+clip clock with exact rig hierarchy checks and optional fallback to new Graph defaults.
- `SceneLoadCoordinator::commit/unload` in the peer branch currently can call `SceneResourceAdapter::release` synchronously when scene resource counts drop. The animation bridge owns only snapshot references, NOT ModelAssetProvider/GPU resource owners. Therefore the actual host must add independent lifetime pins or defer release across two-phase coordination; simply invoking `commitStaged` AFTER a coordinator that already destroyed old Model objects would be unsafe. **This resource-release integration gap is still open and blocks a production integration claim**.
- `MotionWeaveScenePublication` does not support nonblocking `ModelAssetProvider::shutdown`, texture-cache retirement or per-resource GPU fence proofs. A CPU ModelReadLease during sync/prepare is NOT a GPU completion fence.
- The source header is not yet linked from an actual SceneLoadCoordinator-bearing native GameModule; peer scene branches were not merged/rebased or force-pushed.

## Actual verification

ACTUAL PERFORMED: GitHub-connected source reads of both peer scene branches' `SceneLoadCoordinator`, `SceneRuntime`, `SceneAsset`, `PrefabStaticSceneBinding`; current `SceneAttachmentHost`, `EcsAttachmentScene`, `AnimatedSceneAttachmentSources`, `View` APIs and contracts. SHA-guarded GitHub source commits and readback of new header API, staged/committed maps, draw IDs, scene unload/rollback. PR #22 status verified OPEN/DRAFT/UNMERGED.

**NOT RUN:** native Mac CMake configuration/C++ compiler/link, CTest, any scenario test of the new header, real game SceneLoadCoordinator integration, Studio/ModelView render, Metal/Vulkan/GLES shader/pixel tests, sanitizers, CPU/GPU fault injection, model-provider release/fence tests, VRAM or crowd benchmark. This is SOURCE IMPLEMENTATION ONLY, not a confirmed working production feature.

## Mandatory acceptance scenarios

1. Two additive scenes with identical local object keys/child source IDs: distinct presentation Worlds and distinct render IDs, correct single Graph writer per child.
2. Multi-object scene stage failure on object N+1 after object N succeeded: old scene fully unchanged, no leaked visual Graph player or ECS child.
3. Accepted authoritative commit publishes new scene once; rejected/cancelled native commit discards pending without affecting current visuals. No authoritative World mutations from visual adapter.
4. Persistent Replace: old nonpersistent scene unloaded only when absent from complete active SceneState; persistent scene survives.
5. Rollback clears pending/live visual scene epochs; rehydration uses new ECS parent/child handles and source IDs.
6. Reload compatible Graph retains named state/clip phase; incompatible model/rig/child ID safely restarts; no double ticking.
7. Shader/renderer holds CPU ModelReadLease until Renderer3D::render; old/new GPU resources remain owned through actual backend fence completion, with fault injection for failed RHI submission/retirement.
8. Native compile with -Werror, CMake/CTest plus Metal/Vulkan/GLES backend validation, sanitizer and crowd stress.

## Readiness table

| Subsystem | Stage 173 source | What remains |
|---|---|---|
| Universal MotionWeave AnimationGraph/ECS | Incremental implementation | native full regression |
| Independent animated scene children | Existing Stage 161–164 + this bridge | real multi-scene/host GPU test |
| Portable playback / parameters across reimport | Existing Stage 163–164 + stage transfer | native continuity |
| **Transactional cosmetic scene publication** | **Stage 173 source implemented** | compile, live SceneLoadCoordinator host adapter |
| **Two-phase native commit / replace / rollback API** | **Stage 173 source implemented** | actual authoritative application wiring |
| Cross-scene unique render identities | Stage 173 source implemented | real multi-scene shared View |
| Genre-neutral rig/retarget/morph LOD | Stage 165–172 source | complex rigs, perf and pixels |
| Real SceneLoadCoordinator GameModule hook | NOT IMPLEMENTED | peer branch integration, resource pin/release |
| Token-complete GPU Model/Texture provider retirement | NOT IMPLEMENTED | cache/rollback/fence proof |
| Complete CMake/CTest/Metal/Vulkan/GLES/sanitizer/perf | NOT RUN | agreed final coordinated Mac campaign |

## Stage 174 recommendation

Review peer scene branches' actual authoritative object-resource lifecycle, implement an explicit host-owned appearance/provider residency lease (not a shared_ptr to AppearanceAsset alone), then add the real native scene-loader callback/wrapper when those headers are reconciled into the integration branch. Do NOT blindly merge peer branches and do NOT claim no GPU stall until all resource owners and fences are proven. Continue closing outstanding complex facial/IK/editor authoring functionality, then run ONE final native Mac acceptance and fix all compiler/backend errors before considering PR #22 merge. Create an independent Stage 174 handoff after changes.
