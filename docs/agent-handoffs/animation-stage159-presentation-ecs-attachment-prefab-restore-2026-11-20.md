# AIVERSE MotionWeave — Stage 159 handoff: presentation-ECS attachment scenes

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — keep OPEN/DRAFT, do not merge
Previous: [Stage 158](animation-stage158-universal-attachment-layout-transform-2026-11-20.md)

## User's invariant requirements

- **Animation must be completely genre-independent.** No FPS/RTS/RPG/gameplay/network types in MotionWeave, generic View or shared animation libraries. The new scene attachment bridge is an optional **presentation-only** ECS world, NOT authoritative Simulation/physics/gameplay ECS.
- **One animation graph/pose writer per source.** Static child socket visuals are never independent graph players. The parent is sampled by the existing host Animator/Graph exactly once; the generic View uses `HostPrecomputed`. An animated child model with its own graph must be a distinct independently identified source, not silently ticked as a static attachment.
- After EVERY stage: commit an independent handoff with code SHAs, architecture, API, ownership, actual vs NOT RUN tests and precise next steps; keep chronological journals/AGENTS.md and display a full readiness table to the user.
- User requests **finish animation functionality first, then run one coordinated native Mac compile/CTest/GPU/sanitizer/benchmark suite**. Do not claim successful tests or merge PR #22 before the full validation.

## Why this stage could not directly modify the scene loader

A source inspection of the branch revealed `engine/core/ECS.h` with generic `World::create/destroy/add/tryGetConst`. However, `engine/scene` / `SceneLoadCoordinator` and newer static prefab scene files present in other concurrent branches are **NOT in the animation feature branch**: a connected GitHub directory read of `engine/scene?ref=feature/universal-ecs-animation-pipeline` returned 404. We must NOT silently merge/rebase a divergent peer scene branch while other agents edit it. Stage 159 therefore implements the **real generic ECS presentation/prefab bridge** and wires it into the already-existing Studio attachment preview, but **NOT into the external scene loader or gameplay Simulation**. Follow-up integration must be reviewed against the scene agent's newest code.

## Exact code commits and changed files

1. `b0266c3ddbf6278da540e97f578d806005f9b65b` — new `engine/view/EcsAttachmentScene.h`:
   - `view::SceneAttachmentRootComponent` on the externally owned parent Entity: current instance revision, current source Model publication generation and authored child count.
   - `view::SceneAttachmentChildComponent` on each real presentation-ECS child Entity: generation-aware `Entity parent`, instance revision, stable authored ordinal, model path, named socket, enabled flag and full `render::mat4 local/childWorld` affine matrices. **Disabled authored entries also get ECS child entities** for stable authoring roundtrips; they do not import or render models.
   - `EcsAttachmentScene::Instance` contains parent Entity, generation/revision identity, pointer-free `AttachmentLayout`, actual `ImportedAttachmentAuthoring::Compiled` bindings and owned child ECS Entity handles. Runtime borrowed Model pointers occur ONLY in the compiled binding and remain externally provider-owned.
   - `publish(World&,parent,layout,sourceSnapshot,cache,resolver,out,error)` validates the parent and ownership, max 4096 child records, complete pointer-free authored matrix schema via `AttachmentLayoutCodec::write`, and every ENABLED child's resolved published appearance and named bone via `ImportedAttachmentAuthoring::compile`, **before ECS structural changes**. Invalid source rigs, missing children, sockets, borrowed Model cache read leases or resource generations fail before changing the previous output.
   - For a valid candidate, create one real `World::create()` child Entity and attach `SceneAttachmentChildComponent` for every authored item, then replace/create `SceneAttachmentRootComponent` at the parent. After the commit point, replace `Instance` and destroy only previous-generation owned children whose current components still match the old parent/revision. New instance revision is unique and nonzero through an atomic monotonically incrementing source. If an exception occurs before root commit, the newly created children are rolled back; the previous parent/root and bound instance survive.
   - `restore(World&,parent,serializedJson,sourceSnapshot,cache,resolver,out,error)` rehydrates **fresh ECS child Entities** from the same saved source-neutral `AttachmentLayout` JSON instead of blindly reusing stale serialized Entity indices/handles. `save(instance)` emits only the pointer-free user-authored layout. Parent Entity identity is supplied by the host's existing scene world spawn/restore transaction.
   - `valid(World&,instance)` verifies the external parent is still alive, source Model epoch/revision is unchanged, every child Entity is still alive, correctly associated with exactly that parent and authored ordinal, and its source/socket/visibility/affine transforms match the document. This prevents silently drawing an attachment from an allocator-reused Entity slot after deletion/reload.
   - `prepare(World&,instance,cache,worldMatrix,Prepared&,error)` checks the ECS root/children and delegates to the existing `ImportedAttachmentAuthoring::prepare(...,AnimationOwner::HostPrecomputed)`, returning a **move-only frame-owned ModelReadLease** bundle for synchronous View/Renderer3D submission. It does NOT tick a second AnimationGraph.
   - `forget(World&,instance)` removes the root component only if owned by this exact revision, destroys **only** child Entities with matching parent/revision and resets the instance. The externally-owned parent Entity is NEVER destroyed. On destroyed source parent, owned children can still be individually reclaimed, but foreign entities are never destroyed.

2. `1f35c0408ca22d5381400cedf2ae313a95023ec9` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`:
   - Each Studio authored attachment `Session` now owns its own **presentation ECS `World`**, a parent Entity and a `view::EcsAttachmentScene::Instance` in addition to its independent `ModelAssetProvider`, Appearance registry and compiled owner.
   - `Reimport & Publish` builds a complete separate native resource provider and resolves all enabled authored child Model paths/snapshots first. It then creates the presentation root Entity and invokes **real** `EcsAttachmentScene::publish`, generating actual ECS child entities plus current model epoch/socket metadata; failed source imports or compilation preserve previous Studio session and leave its World unchanged.
   - Studio's offscreen parent/child 3D preview now invokes `EcsAttachmentScene::prepare` rather than bypassing the ECS and calling Imported::prepare directly, making destroyed/reused ECS child identities fail closed before a rendering packet can be prepared. Stage 156 single-pose-writer owner remains the external parent preview sampler; no new graph evaluator.
   - Session retirement calls `EcsAttachmentScene::forget` before releasing old appearance/Model owner. Old provider/World generations remain deferred as in Stage 156 until explicit safe GPU completion; no use-after-free of old source Models.

## Public native C++ workflow

```cpp
#include "engine/view/EcsAttachmentScene.h"
using Scene = aiverse::view::EcsAttachmentScene;

aiverse::World presentation;  // cosmetic World, never Simulation World
aiverse::Entity parent=presentation.create(); // host owns this
aiverse::view::AttachmentLayout prefab = /* decoded with AttachmentLayoutCodec */;
Scene::Instance attachments;

std::string error;
Scene::Resolver resolve = [&](const std::string& assetPath) {
    return appearanceRegistry.find(assetPath);
};
if(Scene::publish(presentation,parent,prefab,publishedSource,
                  modelProvider.resources(),resolve,attachments,&error)) {
    // Per frame (on the Device/World owner thread):
    aiverse::view::ImportedAttachmentAuthoring::Prepared frame;
    if(Scene::prepare(presentation,attachments,modelProvider.resources(),
                      worldMatrix,frame,&error)) {
        frame.object.draw.pose=&alreadyComputedParentPose;
        frame.object.draw.nodeWorld=&alreadyComputedNodeWorld;
        genericView.submitOne(renderer,frame.object);
        // The frame's ModelReadLeases release after synchronous submit.
    }
}

// Pointer-free scene/prefab document:
Json saved = Scene::save(attachments);
// Restore after a DIFFERENT World has recreated the parent Entity:
Scene::restore(newPresentation,newParent,saved,newSourceSnapshot,
               newCache,newResolver,recreated,&error);
Scene::forget(presentation,attachments);  // before old World/resource retirement
```

The runtime's auto-generated child Entity handles are NEVER written into JSON and do NOT outlive their original presentation World. Scene loaders must recreate parent entities using their own stable scene key, then call `restore` at the **presentation owner barrier**, after ModelAssetProvider publication and while CPU rig/child ModelReadLeases are valid. The native project's scene loader remains authoritative for parent spawning, asset resolver, hierarchy ownership, asynchronous worker joins and old/new GPU retirement.

## Safety and known limitations

- No genre-specific simulation components or physics required. The runtime ECS child components contain presentation metadata only; there is no collision/damage/AI/gameplay mutation and no new floating-point simulation authority. Child attachments with independent graph/skeleton animation are still explicitly rejected by `ImportedAttachmentAuthoring::compile`; use a separate independently identified animation source for those future features.
- A parent can own at most **one** `SceneAttachmentRootComponent` through this helper. Another caller cannot replace an existing root unless it supplies the matching `Instance` revision and parent handle. If multiple subprefabs need siblings on the same Entity, add a distinct root namespace/container in a future stage.
- Every authored child occupies an ECS Entity for indexing and persistence, even if disabled. Rendering does NOT issue one draw per child ECS Entity: `View::submitOne` uses the compiled static `ModelAttachment` packet; this preserves the existing single-pose writer and batched parent+socket submission contract.
- The bridge caches source/child Appearance metadata and compiled socket plans but owns NO GPU Models/textures or device resources. Full CPU/GPU lifetime still resides with ModelAssetProvider; `Prepared` read leases cover only immediate owner-thread render. Hosts must call `forget` before destroying World/model caches; the old provider may only be freed after an actual GPU completion proof.
- `publish` is transactional for **ordinary non-COW presentation worlds and no concurrent structural mutation**; if a custom World pool clone/destroy throws after replacing the parent root, general OOM strong exception safety cannot be asserted. The bridge is not suitable for an authoritative COW gameplay rollback World; host's scene loading owner must guarantee exclusive structural access.
- Existing `engine/scene/SceneLoadCoordinator` source is not on the current feature branch. **Automatic SceneLoadCoordinator import of attachments, scene save-game integration and prefab scene activation/unload are NOT COMPLETE.** This header exposes the intended integration contract, and the Studio editor is the first real consumer.

## Actual verification vs NOT RUN

**ACTUAL:** connected GitHub read of Stage 158 handoff, existing `engine/core/ECS.h` Entity/World APIs including generation-aware destroy, shared `AttachmentLayout.h`, `ImportedAttachmentAuthoring.h`, Studio attachment authoring/preview and source branch PR state. Confirmed `engine/scene` missing from feature branch via explicit directory request (404). Created new generic bridge through the GitHub connector; integrated Studio Session with ECS publication and prepared-draw validation using an SHA-guarded code update. GitHub readback checked both files and all key bridge entry points. No main or peer branch mutation.

**NOT RUN:** CMake native compilation/link, CTest, successful CI for the Stage 159 HEAD, actual Studio ECS child instantiation in running app, scene/prefab game integration, Metal/Vulkan/GLES GPU visuals, ASan/TSan/UBSan, fault injection, large-world/crowd memory or benchmark, `bash tools/animation/validate-final.sh`. Code readback is NOT a passing native test.

## Required readiness table — Stage 159

| Subsystem | Source status | Remaining work |
|---|---|---|
| Genre-independent MotionWeave animation ECS / Graph | Implemented incrementally | Real native graph/worker tests |
| Named hot reload and source Model epoch | Implemented incrementally | Integration and fault injection |
| Animation LOD / compiled retarget | Partial | Complex rigs and crowd performance |
| Morph target and advanced skeletal authoring | Partial | Rich Studio importer/UI |
| Static imported Model attachments / compiled sockets | Core source exists | GPU/memory/visual parity |
| Shared pointer-free affine attachment prefab layout | Stage 158 source implemented | Native codec roundtrip and bad-data tests |
| **Presentation ECS child entities and parent binding** | **Stage 159 source implemented** | Compiler and real scene stress tests |
| **ECS attachment replace/unload/restore** | **Stage 159 source implemented** | Runtime rehydrate/rollback/failure tests |
| Studio 3D preview consumes ECS attachment relationships | Stage 159 integrated in source | Real GUI/scene visual validation |
| ModelView file watcher / hot imported model reload | Stage 157 source implemented | Live disk/graphics tests |
| Automatic SceneLoadCoordinator / gameplay prefab integration | NOT IMPLEMENTED | Peer scene branch reconciliation and host adapter |
| Animated child with own Graph source | NOT IMPLEMENTED | Identity, one-writer and independent source workflow |
| True token-complete provider GPU retirement | NOT IMPLEMENTED | Texture/cache/rollback safe full-session API |
| Metal/Vulkan/GLES GPU parity | NOT VERIFIED | Native backend tests |
| Full CMake/CTest/sanitizers/performance | NOT RUN | One final coordinated Mac test pass |

## Next Stage 160

1. Coordinate the current `engine/scene` branch with the owner of SceneLoadCoordinator. In a reviewed, disposable integration worktree adapt the existing `SpawnPrefab`, activation/unload callbacks and scene/prefab host serialization to `EcsAttachmentScene::restore/publish/forget`. Do not merge divergent branches or use the authoritative Simulation as the cosmetic attachment World by default.
2. Extend the presentation bridge for animated child entities only if each child obtains its own stable source ID, compiled Graph, immutable Model and EXACTLY one graph writer. Add optional per-child scene identities or authored attachment GUIDs for stable incremental reconcile without replacing all ECS child entities.
3. Continue true token-driven full-owner ModelAssetProvider shutdown only after mapping resident/shared Model textures and rollback queues. No bypass of `waitForResourceIdle()` until GPU-safe ownership proof is established.
4. Complete remaining complex retarget/morph/LOD/scene authoring functionality, then one comprehensive native Mac suite CMake/CTest/Metal/Vulkan/GLES/ASan/TSan/perf and repair. Keep PR #22 Draft until verified. After every stage append a standalone handoff + readiness table.
