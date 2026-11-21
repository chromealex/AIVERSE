# AIVERSE MotionWeave — Stage 161 handoff: independent animated child graph sources

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT / NOT MERGED
Previous: [Stage 160](animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md)
User-facing guide: [docs/animated-scene-attachments.md](../animated-scene-attachments.md)

## User requirements and architecture

- **Genre-independent** MotionWeave animation core, generic View, ECS presentation scene/attachment/retarget APIs. No FPS/RTS/RPG/gameplay/physics/network logic in shared modules.
- ONE owner of EACH `AnimationGraph` player's state/clock/pose. A static attachment inherits its parent's pose and never evaluates another Graph. A separately animated child owns a distinct stable scene/object/child source identity and distinct MotionWeave graph player in an independent presentation ECS World, then submits a graphless precomputed View Object.
- Continue feature completion first; run ONE full native Mac C++/CTest/Metal/Vulkan/GLES/sanitizer/performance suite at genuine feature closure. After every stage create a separate handoff and print the user-required complete readiness table. Current stage is SOURCE IMPLEMENTATION ONLY, NOT TESTED.

## Stage 160 -> 161 missing feature

Stages 150–160 supported static child Models on named parent sockets, exact Model-publication epochs, source-neutral JSON, ECS child entities and stable prefab child IDs. But `ImportedAttachmentAuthoring::compile` deliberately rejects animated child `Asset::graph`. A child with its own skinning/montage Graph could not be driven and drawn independently. Attaching its Model as a static socket draw would inherit the wrong source pose or spawn a second Graph writer if both View and the host attempted evaluation.

Stage 161 separates STATIC socket draws and INDEPENDENT animated child graph sources, retaining child ID across reimport and never updating the same graph twice.

## Exact source commits and files

1. `7fa7cba2151eb83b68b494a156b62e422de3fdb7` — `engine/view/AttachmentLayout.h`: appended optional `AttachmentLayoutItem::animated=false`; serializes `"animated":true`; validates Boolean type and requires nonempty stable `id` for enabled animated children. Legacy layouts remain source-compatible.
2. `5efa0a9972b39047c044e1fedc687ad93c2b6bc0` — `engine/view/EcsAttachmentScene.h`: append `SceneAttachmentChildComponent::animated`, include it in immutable authored equality checks, and SKIP every enabled `animated=true` child when compiling the parent's STATIC socket draw bundle. It remains a REAL ECS child Entity with stable authored identity.
3. `36a1508f56e1f9682760f476bae40770e5c41366` — new `engine/view/AnimatedSceneAttachmentSources.h`:
   - A reusable, genre-free `AnimatedSceneAttachmentSources` with private `World animationWorld_` and exactly one `lib::animation::MotionWeaveSourceBridge`, not another editor/game evaluator. `Input` identifies `{sceneId, objectKey, authoredChildId}`, published child Appearance snapshot and graph parameter overrides.
   - `sync(scenes,cache,fullSpan,dt,stats,error)` preflights each child against the live `SceneAttachmentHost::resolveChild`, authored enabled+animated mode, published nonzero parent/child Model epochs, immutable child `graph->model()==model`, parent/child owner-thread `ModelReadLease`s and compiled named parent socket. Duplicate source identity/missing Model/Graph/rig fails BEFORE the bridge step. Existing key retains monotonic source ID; new keys allocate a fresh nonzero ID (never reuses after retirement). The generation-aware current child `Entity::packed()` is supplied as the logical source generation; hot-reimport/entity replacement restarts the correct child player.
   - `MotionWeaveSourceBridge::sync` then `stepAnimationPresentation(animationWorld_,dt)` execute ONCE, only on the presentation owner thread, before rendering. Missing inputs retire their graph players. All child Models are cache-read-leased through evaluation.
   - `prepare(scenes,cache,key,parentModelDraw,parentNodeWorldMatrices,Prepared&,error)` validates current child entity, parent/source published epoch, cache leases, evaluated child graph pose/matrix shape. It composes the child WORLD transform as parent-world × evaluated parent bone × authored local × childWorld and returns a move-only `Prepared` retaining both parent/child ModelReadLease and appearance metadata. It sets the independent child's evaluated `draw.pose` / `draw.nodeWorld` from the ONE presentation graph writer and deliberately `draw.graph.reset()`, so generic View cannot tick it again.
   - `clear` retires player links before Model/Graph provider retirement; `sourceId(key)` reports the current mapped source. All per-frame borrowed poses are valid only until the next child `sync/step`.
4. `04778255eeb09c1495d4655f81c9f2c63309f864` — `engine/view/AttachmentLayout.h`: appended `animationGraph` string asset key; enabled animated entries require BOTH stable ID and Graph file/key. JSON read/write validate its type and length; no GPU/Graph pointers serialized.
5. `d3fab4d16ab396886c3cb61364d28b49cf87dfee` — `engine/view/EcsAttachmentScene.h`: add authored `animationGraph` key to child ECS component and equality preflight.
6. `cc5728567dde95450f719b6dc2a93ae8458acc46` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`: preserve `animated` and graph path in Studio JSON Load/Save; author `Independent animation` and `Child AnimationGraph file` controls; staged sessions store separate published animated child appearance snapshots, and the window owns `AnimatedSceneAttachmentSources` plus separate render-only child View.
7. `42849691010600d0e8d5564194d3dc7623a3be61` — Studio: for each enabled animated child, load its REAL imported child Model through its own staged provider, publish its authored external `animationGraph` using `Registry::Request.graphReader` and a nonzero source ID, and keep compiled Graph snapshot in `Session::animatedChildren`. A missing file/invalid Graph rejects staging and preserves last-good session. Static children continue using the original `ImportedAttachmentAuthoring` without Graph. Scene host publishes ECS children for BOTH modes but compiles static socket draws ONLY for static.
8. `1671c702aae14329c1de67bb2a273eccddb55e10` — Studio: its real offscreen 3D viewport draws parent+static children and separately animated child skinning/world attachments using graphless `AnimatedSceneAttachmentSources::prepare` packets and another render-only `View`, preserving frame-local read leases through `Renderer3D::render`. `animatedSources_.clear()` runs before provider generation changes and teardown.
9. `5f4b6b93a9f76b6a8e7b510099d00d886950a2be` — `AnimatedSceneAttachmentSources.h` uses the same public `<aiverselib/animation/...>` include-root contract as generic View.
10. `55bab0548e98d3ed1c02191b8ac5cc84f271758f` — Studio: move child MotionWeave `sync` and graph evaluation to `EditorWindow::update` on the pre-draw owner structural boundary (once per frame), and leave ImGui `draw` to consume fully evaluated graphless poses. The UI Play pause sets dt=0; the Studio parent's own preview sampler still produces the parent bone matrices.
11. `ed90f7788edfde1e2efb22754ba51ec162b3eabe` — `apps/modelview/main.cpp`: roundtrip `animated` and `animationGraph` in the same shared attachment JSON; the standalone ModelView's parent-only Animator explicitly rejects an enabled independent child rather than silently duplicating static and animated draws. Its own child Graph ECS integration remains a separate feature.
12. `278fdd924da2251a6643995a1ffe40855b9a9215` — [docs/animated-scene-attachments.md](../animated-scene-attachments.md) with concrete layout JSON, native C++ source `sync/prepare`, Studio workflow, single-writer and GPU lifetime contracts.

## Native API sketch

```cpp
using Animated=aiverse::view::AnimatedSceneAttachmentSources;
Animated animationChildren;
std::vector<Animated::Input> inputs{
    {{sceneId, "hero-42", "lantern"}, publishedAnimatedChildSnapshot, {}}
};
Animated::Statistics stats;
if(animationChildren.sync(sceneHost,modelCache,inputs,dt,stats,&error)) {
    Animated::Prepared prepared;
    if(animationChildren.prepare(
          sceneHost,modelCache,
          {sceneId,"hero-42","lantern"},
          parentDraw,evaluatedParentNodeWorld,prepared,&error)) {
        // Child Model pose was already evaluated ONCE in child host World.
        // The graph is NULL in this Object so the generic View will NOT
        // perform another graph evaluation.
        childView.submitOne(renderer,prepared.object);
    }
}
// Call animationChildren.clear() before old CPU Model/Graph retirement.
```

`Input::appearance` must point to a REAL, published, immutable child appearance with nonzero Model epoch and compiled `AnimationGraph` whose source Model matches. Host may use `AppearanceAssetRegistry::Request.animationGraph`, its `graphReader` and nonzero `sourceId` to compile the asset. Shared child Model/Graph resources remain host-owned. Parent and child resources MUST be in the same `ModelResourceCache` and pinned via short-lived owner-thread read leases through graph update and synchronous renderer submission.

An input list covers the complete active child set each frame: missing children are retired by `bridge_.sync(removeAbsent=true)`. The ID mapping is unique within this host instance (never reused during that host's lifetime) and its logical generation equals the current child ECS Entity's generation-aware packed ID. The scene/object/child authored identity is stable across hot-reimport; the allocated ECS Entity handle itself is not.

## Safety, limitations and what remains

- The parent/static attachment path NEVER compiles an `animated=true` child. An independent child is evaluated by ONE separate MotionWeave source bridge and has its OWN local Pose/World matrices. The generic child render-only View receives a graphless precomputed Object. No second graph player and no accidental double static draw.
- Parent socket world composition uses the actual parent source rig, evaluated node matrices and authored local/child transforms. Parent + child ModelReadLeases remain active through synchronous draw; GPU device retirement is still a host obligation and NOT satisfied by these CPU leases.
- Studio supports authored external Graph asset path and attempts to compile it with the existing Appearance registry; this is not a general animation graph creation/timeline/Control Rig authoring UI. If imported Graph fails, the staged new session rejects and preserves last-good resources.
- ModelView only **preserves** the new fields and rejects enabled animated children with a clear message. It does not yet have an independent child MotionWeave World; do not claim native ModelView animation parity.
- The child source host reports false on preflight error without advancing Graph. An exception after bridge World mutation may leave bridge ECS needing next owner sync; do not make absolute exception-atomicity claims across a throwing graph evaluation.
- The source ID allocator is unique within the dedicated child source host, NOT guaranteed globally collision-free with separately instantiated View sources; submit to a separate child render-only View or namespace caller IDs appropriately.
- Independent source evaluation currently uses single-threaded `stepAnimationPresentation`, intentionally one scheduler owner per source. Worker-pool tuning, large-crowd budgets, child LOD/morph retarget and cross-host playback snapshot continuity remain follow-ups.
- The `engine/scene` SceneLoadCoordinator resides on separate branches `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration`, NOT merged into this animation branch. No direct authoritative game scene callback, scene hot streaming or built-in prefabs activation is claimed.
- Fully tokenized/nonblocking `ModelAssetProvider::shutdown` remains NOT IMPLEMENTED; shared ModelResourceCache/texture rollback ownership still requires proper full GPU completion. Metal/Vulkan/GLES correctness remains unknown.

## Actually verified vs NOT RUN

**ACTUAL:** connected GitHub source audit of Stage 160 handoff, existing MotionWeaveSourceBridge, EcsAnimationSystems and `stepAnimationPresentation`, universal `View::Object/submitOne`, AppearanceAssetRegistry Graph compilation, ModelComposition socket composition, Source/Child ECS components and Studio/ModelView host code. SHA-guarded commits added new public header, explicit authored Graph schema and Studio independently compiled child Graph sources, rendered them with the separated View, then moved scheduler step out of the UI draw into owner `update`. Source readback must verify latest paths, documentation and PR status. No main or peer branch changes.

**NOT RUN:** native CMake configure/C++ compiler/link, current green CI, CTest, real Studio Child Graph compilation/skin pixels, ModelView runtime, actual CPU/GPU lifetime after hot reimport, Metal/Vulkan/GLES GPU, ASan/TSan/UBSan, fault injection, crowd/VRAM benchmark, `tools/animation/validate-final.sh`. User requests one full Mac test suite after feature completion. Committed source is NOT a passed test.

## Stage 161 readiness matrix

| Subsystem | Source status | Outstanding work |
|---|---|---|
| Genre-neutral MotionWeave ECS / AnimationGraph | Implemented incrementally | Full native graph/worker/regression |
| Named Graph hot reload / published asset epoch | Implemented incrementally | Cross-host hot replacement parity |
| Source identity / stable prefab child key | Implemented incrementally | Scene unload/restore and collision tests |
| Whole-model Animation LOD | Partial | Complex rigs, crowd and visual verification |
| Skeletal retarget / morph target authoring | Partial | Complex imported rigs, complete Studio UX |
| Static imported Model/socket attachments | Source implemented | Real GPU and lease lifetime parity |
| Presentation ECS scene host + restore/forget | Source implemented | SceneLoadCoordinator cross-branch integration |
| **Independent child MotionWeave graph host** | **Stage 161 source implemented** | Native one-writer, reimport and perf tests |
| **Animated-vs-static attachment split** | **Stage 161 source implemented** | Duplicate-draw/pose regression tests |
| **Studio child Graph file authoring + 3D preview** | **Stage 161 integrated in source** | Native Studio Graph import and GPU visuals |
| ModelView independently animated children | **NOT IMPLEMENTED** | Child Graph owner integration; currently explicitly rejects |
| Automatic SceneLoadCoordinator activation/unload | NOT IMPLEMENTED | Reviewed scene branch adapter |
| True full-provider GPU-token retirement | NOT IMPLEMENTED | Shared cache/texture/rollback ownership |
| Metal / Vulkan / GLES | NOT VERIFIED | Real backend tests |
| Full CMake / CTest / ASan / TSan / benchmarks | NOT RUN | Final coordinated Mac validation |

## Next Stage 162

1. **ModelView independent child source integration**: mirror Studio's separate MotionWeave owner, Graph compilation and correct CPU Model/GPU lease transaction, while keeping parent's existing Animator as its ONLY owner.
2. Tighten animated child Graph asset hot-reimport continuity, budget/performance, correct child model LOD/morph retarget and per-frame source identity on large crowds.
3. Integrate scene loader hooks only after reviewed reconciliation of `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration`; do not auto-merge, rebase or force-push peer branches.
4. True whole-provider GPU token-complete shutdown only with the necessary shared texture, rollback queue and GPU fence proof. Do not remove current safe waitForResourceIdle() based on assumptions.
5. Finish all features, run the user's ONE coordinated native Mac CMake/CTest/Metal/Vulkan/GLES/ASan/TSan/visual/VRAM/crowd suite, fix errors, then assess PR #22 Draft status. Per-stage handoff and user-visible readiness table mandatory.
