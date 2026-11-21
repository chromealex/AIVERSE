# AIVERSE MotionWeave — Stage 160: stable prefab child identity + scene attachment lifecycle

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — KEEP OPEN/DRAFT, no merge
Previous: [Stage 159](animation-stage159-presentation-ecs-attachment-prefab-restore-2026-11-20.md)
User documentation: [SceneAttachmentHost](../scene-attachment-host.md)

## Mandatory user requirements

- **MotionWeave and the universal View/ECS presentation components are completely genre-independent.** No RTS, FPS, RPG, network, gameplay-action or authoritative simulation logic may leak into shared animation/attachment modules.
- Every animated Model source has ONE graph/pose writer. Existing static socket attachments share their host-computed parent pose and never tick a second Graph. **Distinct animated children with independent Graphs remain pending**, not silently treated as static child attachments.
- Continue feature completion before performing the user's ONE coordinated native Mac CMake/CTest/Metal/Vulkan/GLES/sanitizer/performance pass.
- After EACH stage: independent handoff + journal / AGENTS.md update + user-visible status table. Report actual verified code reads separately from NOT RUN tests. Keep peer branches and `main` unchanged.

## Scene branch audit performed before implementation

Previously Stage 159 established that this animation feature branch contains `engine/core/ECS.h` but NOT `engine/scene`. In Stage 160 the exact existing peer branches were discovered and read:

- `feature/scene-runtime-multiworld` — `engine/scene/SceneAsset.h`, `SceneLoadCoordinator.h`. Actual `SceneLoadCoordinator::SpawnPrefab` callback:
  `bool(World&, Entity, const SceneObjectAsset&, std::string& error)`, invoked at a deterministic owner structural/tick barrier. `SceneObjectAsset::id` is a stable **scene-local uint64_t** independent from ECS Entity allocator; `properties` is Json.
- `feature/ecs-static-prefab-scene-integration` — `engine/scene/PrefabStaticSceneBinding.h` shows the source-level adapter chaining/callback mechanism, and `engine/scene/AGENTS.md` requires scene-owned authoritative ECS lifetime, per-world identifiers and unloading through SceneLoadCoordinator.

**Neither scene branch was merged/modified.** An animation attachment scene is a cosmetic **presentation World** with separate parent visual Entity; passing the authoritative `SpawnPrefab` Entity to the separate visual World is invalid. Stage 160 adds a host adapter that can be wired after a reviewed cross-branch integration, not a false claim of full SceneLoadCoordinator integration.

## Exact code changes and commits

1. `a981b9d14ce41b52bac8d6de965c1c96412138f8` — `engine/view/AttachmentLayout.h`:
   - Append optional `AttachmentLayoutItem::id` AFTER legacy aggregate fields, retaining existing constructor compatibility.
   - `AttachmentLayoutCodec::read/write` load/save a non-empty `id` string (max 128 chars) and reject duplicate explicit IDs, malformed types or excessively long IDs **before** replacing the caller's last-good document. Absent/empty IDs preserve old positional/ordinal behavior.
   - IDs are CPU-only authoring keys; never serialized CPU Model pointers, ECS handles or GPU resources.
2. `ea24e0b2bdffa2fa84164f94a008c885fe0a8426` — `engine/view/EcsAttachmentScene.h`:
   - Append `SceneAttachmentChildComponent::authoredId`, populating it from the pointer-free layout for each real child Entity and checking exact authored match in `valid`. Historical C++ aggregate initializers remain source-compatible.
3. `42377b455bb23e2ddcbf79f01505ceb79489a5ea` — NEW `engine/view/SceneAttachmentHost.h`:
   - A generic map keyed by `{uint64_t scene instance, std::string persistent prefab object key}`, independent of ECS allocator positions and Model publication. The host owns ONE separate presentation World, one `SceneAttachmentHost` and native ModelAssetProvider/leases; this adapter owns ONLY `EcsAttachmentScene::Instance` metadata/bindings/child handles.
   - `activate(sceneKey, visualParent, Json layout, publishedSourceSnapshot, cache, childResolver, error)` installs/replaces a prefab's attachments through `EcsAttachmentScene::restore` at a visual structural barrier. The map entry is reserved BEFORE World mutation, so a map allocation failure does not strand ECS child entities. When a scene replacement changes visual parent, prepare the new instance/children FIRST and forget old owned ones only after accepted publication.
   - `deactivate(sceneId, object)` releases a single object; `unload(sceneId)` releases all scene-owned attachments without destroying host-owned parent Entities; `clear()` explicitly forgets all instances before World/Model cache teardown. `save/prepare/find/contains/size` support source-neutral scene workflows. No scene-loader or genre dependency.
4. `6d175d17bf21455be62a29ad19bfd1f2537ab14f` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`:
   - Each Studio Session owns `World presentation` and `std::unique_ptr<SceneAttachmentHost>` bound to it; source/children remain in the SAME independent ModelAssetProvider.
   - Studio `Reimport & Publish` binds a real presentation parent under stable key `{1,"preview"}`; offscreen rendering now uses `SceneAttachmentHost::prepare` (which checks ECS parent/child generation). Session retirement invokes `host->clear()` before GPU resource release. Existing editor Graph/Model previews retain independent owners.
   - The authored Studio UI preserves/edits the optional `Prefab child ID`, and its JSON load/save/scene compilation retain that field.
5. `8089ac28e16de820124c79747e99eac9e83e26de` — `apps/modelview/main.cpp`: ModelView's authored AttachmentSpec now appends stable `id`, roundtrips it through shared AttachmentLayoutCodec and passes it to the common runtime `childFromLayout` conversion, without modifying legacy file semantics.
6. `7dfa1d81632fcc5e4dad4146c404e1fb4e63065c` — `engine/view/SceneAttachmentHost.h`: `saveScene(sceneId)` exports deterministic, sorted, pointer-free `[{object,layout}]` for native per-scene documents; its host restores visual parent identities externally. The `find` signature avoids false `noexcept` when constructing a string-backed key can allocate.
7. `e13690580e253ccfe9e7290b87adb8023f8b10f5` — `apps/modelview/main.cpp`: validate the entire authored layout (including disabled children) with AttachmentLayoutCodec before starting GPU imports, so duplicate IDs fail preflight. Add editable `Prefab child ID` to ModelView's Attachments UI.
8. `b66b40e511ad0eeceea6313a935aa7acc55c8ca7` — `apps/modelview/main.cpp`: catch Save/Codec invalid-id or transform exceptions and fail before truncating a formerly valid JSON file, logging a user-visible authoring error.
9. `64896de81c0b98a1835a7e8d56ea0c7911a7ef0e` — `engine/view/EcsAttachmentScene.h`: `resolveChild(World&, Instance&, authoredId)` returns a current generation-safe Entity only after `valid` confirms the entire scene; returns `kNullEntity` for missing/legacy IDs or stale parent/child handles. IDs are NOT a guarantee that the same Entity handle survives hot reload.
10. `d1d136ace54f2b5acf758b29b051516e975ca4a4` — `engine/view/SceneAttachmentHost.h`: `resolveChild(sceneId,objectKey,childId)` forwards stable ID resolution to the currently active instance.
11. `5f80393ac2677a268ec1f9e737c935f0b66bd65c` — NEW [docs/scene-attachment-host.md](../scene-attachment-host.md): public C++ usage, scene+object ID relationship, prefab JSON, example `SpawnPrefab` adapter contract and precise limits.

## Public API

```cpp
aiverse::World visual; // isolated cosmetic World, not Simulation::world()
aiverse::view::SceneAttachmentHost host(visual);
aiverse::Entity visualParent=visual.create();

bool ok=host.activate(
  {sceneId, std::to_string(authoredSceneObjectId)},
  visualParent,
  authoredAttachmentsJson,
  publishedSourceAppearance, assetProvider.resources(),
  [&](const std::string& path) { return appearances.find(path); },&err);

auto latestChild=host.resolveChild(
  sceneId,std::to_string(authoredSceneObjectId),"left-hand-tool");
aiverse::Json sceneManifest=host.saveScene(sceneId);

aiverse::view::ImportedAttachmentAuthoring::Prepared prepared;
if(host.prepare(sceneId,std::to_string(authoredSceneObjectId),
                assetProvider.resources(),worldMatrix,prepared,&err)) {
  prepared.object.draw.pose=&computedParentPose;
  prepared.object.draw.nodeWorld=&computedParentNodeWorld;
  genericView.submitOne(renderer,prepared.object);
}

host.unload(sceneId); // at native scene-unload owner barrier
host.clear();         // before destroying visual World or Model cache
```

In a future reviewed integration `SceneLoadCoordinator::SpawnPrefab` supplies stable `SceneObjectAsset::id`; the native host maps its authoritative scene/object pair to the **separately owned visual parent** and calls `activate` at the correct presentation owner barrier. The per-object attachment JSON is portable across rebuilt Worlds. Reload/restore produces fresh Entity handles but preserves authored scene+object+child keys. `unload` removes only scene-owned visual children; it never deletes the native scene's authoritative entities.

## Limitations and ownership

- **Auto SceneLoadCoordinator binding still NOT IMPLEMENTED** on this branch: `engine/scene` headers are not available here. Neither branch was merged, and no authoritative Simulation ECS World was mutated.
- Stable child IDs are an authored referent, not immutable runtime Entity handles. The Stage 159 `EcsAttachmentScene::publish` still replaces its full child generation on accepted publication. A separate keyed incremental reconcile algorithm (if needed) must be authored carefully, with strict error rollback and worker boundary.
- **Independent animated child Graphs NOT IMPLEMENTED** in this stage. Their stable authored IDs are suitable for future host-provided animation source identities, but the current `ImportedAttachmentAuthoring` intentionally rejects child appearance Graphs as static socket attachments. Do not double schedule one Graph or claim a complete animation child pipeline.
- The adapter owns only presentation ECS components/metadata and compiled source/child appearance references. It does not own CPU/GPU Models, file readers, worker jobs, ModelReadLease outside `Prepared`, device tokens, authoritative scene state or game-specific behavior. Asset residency/fences remain host-owned.
- Real full-GPU-token provider retirement remains not implemented: `ModelAssetProvider::shutdown` and shared ModelTextureCache/rollback queues still require correctly proven complete GPU idle. No unsafe bypass.
- `SceneAttachmentHost::activate` must be called exclusively on the presentation owner at a structural barrier; callers must arrange asset import before using it and unload/clear before World destruction. The adapter does not introduce its own tick, graph update or renderer draw pass.

## Actual checks vs NOT RUN

**ACTUAL:** connected GitHub read of branch Stage 159 handoff, EcsAttachmentScene, AttachmentLayoutCodec, SourceBridge, ECS World, Studio/ModelView, source and peer scene runtime branches' `SceneAsset`, `SceneLoadCoordinator` and `PrefabStaticSceneBinding` APIs. Wrote new generic host, added optional authored child IDs and metadata resolution, connected Studio and ModelView, applied JSON duplicate-ID validation and user-guide change via SHA-guarded GitHub commits. Readback of files will be checked after documentation. No main/peer branch mutation.

**NOT RUN:** native CMake or compiler/link, current green CI, CTest, actual Studio/ModelView scene import, real scene loader integration, Metal/Vulkan/GLES, GPU-fence failure injection, ASan/TSan/UBSan, VRAM/crowd benchmarks, `tools/animation/validate-final.sh`. Source existence/readback must NOT be reported as a verified runtime.

## Required status table — Stage 160

| Subsystem | Source status | Remaining |
|---|---|---|
| Genre-independent MotionWeave ECS / AnimationGraph | Implemented incrementally | Final native worker/graph/pose tests |
| Graph hot reload / published Model epoch | Implemented incrementally | Real import mismatch/failure tests |
| Whole-model animation LOD | Partial | Complex rig and high crowd tests |
| Retarget/morph authoring | Partial | Full importer/editor and advanced parity |
| Static compiled sockets and imported child Models | Implemented source | GPU/lease/visual validation |
| Shared affine attachment JSON | Stage 158 implemented source | Full JSON negative/roundtrip tests |
| Presentation ECS child/parent/restore/forget | Stage 159 implemented source | Native generational scene regressions |
| **Optional stable prefab child authored IDs** | **Stage 160 implemented source** | Real editor roundtrip and duplicate-ID tests |
| **SceneAttachmentHost activate/unload/saveScene/prepare** | **Stage 160 implemented source** | Scene loader callback integration and native test |
| Studio and ModelView authored ID UI/roundtrip | Stage 160 source integrated | GUI and render checks |
| Actual SceneLoadCoordinator callback binding | NOT IMPLEMENTED | Reviewed scene branch integration worktree |
| Animated child independent AnimationGraph sources | NOT IMPLEMENTED | One-writer separate source lifecycle |
| True token-safe provider GPU shutdown | NOT IMPLEMENTED | Shared texture/cache/rollback GPU architecture |
| Metal/Vulkan/GLES visual parity | NOT VERIFIED | Real backend frame/fence tests |
| Native CMake/CTest/sanitizers/perf | NOT RUN | Final coordinated Mac suite |

## Next Stage 161

1. Implement or stage a **genre-neutral independent animated child source** contract: one stable source ID per `sceneId + prefabObjectKey + authoredChildId`, sourced by an explicit host-provided graph and published Model epoch. Ensure animated child is NOT also rendered/ticked as a static imported attachment and use one MotionWeave graph owner.
2. Revisit Stage 159 full-child replacement if stable authored ECS Entity handles are required for live editing; any incremental reconcile must preserve transaction rollback, source Model lifetime and parent ownership.
3. Connect SceneAttachmentHost to real `SceneLoadCoordinator::SpawnPrefab` and scene unload only when the `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration` changes can be safely reviewed in a disposable integration worktree. Do not auto-merge/rebase/force push peer branches.
4. Finish real GPU-token complete provider shutdown only after texture/cache/rollback ownership redesign. Then complete complex skeletal/morph retarget authoring and one comprehensive Mac native suite, repair failures and assess PR #22 readiness.
5. Continue independent handoffs, AGENTS.md/chronological updates and the user-visible readiness table each stage.
