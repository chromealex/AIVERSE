# AIVERSE MotionWeave / Universal View — Stage 157 handoff

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22
Previous: [Stage 156](animation-stage156-studio-attachment-preview-deferred-retirement-2026-11-20.md)

## User's binding requirements

**Animation is completely independent of genre:** `MotionWeave`, `engine/view`, common retarget/animation ECS code and Model resource APIs must contain no RTS/FPS/RPG/gameplay/network-specific behavior. The native ModelView is just an example host. Per visual source, only ONE animation evaluator may advance the pose; for ModelView that is the existing `render::Animator`, then a prepared graphless `View` draw.

**Workflow:** complete animation functionality first; run ONE comprehensive native Mac compiler/CTest/GPU/sanitizer/performance validation after feature freeze, not isolated tests after each stage. After EVERY stage, add a standalone agent handoff and a user-visible readiness matrix. PR #22 stays Draft/unmerged until that final validation and reviewed integration.

## Goal

Stage 154 could author, attach/detach, and rebuild socket layouts against **currently cached** real parent/child Models, but clicking Rebuild never imported newly edited file bytes. Replacing an individual `Model*` was NOT safe: `Instance::Animator`, the current `render::Pose`, `AppearanceAssetRegistry::Asset`, socket bindings and `ModelResourceCache` held separate borrowed references to the previous source's rig.

**Stage 157 implements whole-scene, staged on-disk asset reimport in ModelView.** It builds a new independent ModelAssetProvider generation, all parent Animator instances (preserving compatible named clip phase), published appearance snapshots and compiled child socket bindings BEFORE replacing the active scene. A failed Model import, GPU upload, rig mapping or child socket leaves the old visible scene intact. Old generations remain resource-owner-held until an actual GPU idle proof.

## Exact implementation commits and touched files

All changes on `feature/universal-ecs-animation-pipeline`, no main/other-agent branch edits.

1. `20188ec03a2982238ffd93e45c9dc3acad1e3c08` — `apps/modelview/main.cpp`:
   - Refactor native source `ModelAssetProvider` and `AppearanceAssetRegistry` to replaceable `unique_ptr` owners.
   - `RetiredNativeScene` aggregates old provider, old published metadata and old `Instance` vector with borrowed Animator Model pointers; destruction order ensures instances/metadata are discarded before the provider. `retiredNativeScenes` retains old generations across GPU in-flight submissions.
2. `dd153a91609bfca13d8ce29b5388192ef4053420` — same file:
   - Extract `buildAttachmentLayout(ModelAssetProvider&, AppearanceAssetRegistry&, vector<Instance>&, error)` from the original `rebuildAttachments()`. It stages children imported through the common `ImportedAttachmentAuthoring::importChild`, publishes each source in the supplied registry, compiles all socket bindings using the same provider and swaps per-instance compiled snapshots only after all parents/children validate.
   - Ordinary **Rebuild** uses the live resident provider as before; full file reload calls the SAME implementation on a new provider and new instances.
3. `7f8f04733bf1f0db77b08b207e29c358cb0d1476` — same file:
   - `reimportModelFiles()` rereads EVERY parent file through a NEW `ModelAssetProvider`, creates replacement `Animator`s bound to the new `Model*`, preserves world placement/variant and chooses clips by **name**. When both previous/new clip durations are finite and positive, it transfers normalized layer-zero phase through `Animator::seek` (without gameplay notifies). Missing/renamed clips fall back to safe bind pose instead of interpreting previous index against another rig.
   - The function evaluates new bind/clip poses and world matrices for the new Models, compiles the complete child layout, allocates the old generation container, then atomically transfers staged provider + appearance + instances into the live scene. A failure before publication defers ownership of any partially GPU-uploaded candidate and leaves live instances unchanged.
   - `deferNativeScene` retains previous/scratch GPU-owning providers; no active ModelReadLease can span the replacement. An advisory cap of 8 retired generations triggers `collectRetiredNativeScenes()` with explicit `Device::waitForResourceIdle()`. On no GPU completion, it keeps old owners alive instead of destroying resources referenced by GPU work. This is **deferred full-owner retirement**, NOT genuine per-provider async token release.
4. `73b6d14bb2c6babf093428cbfbebbbd2e9e8be16` — same file:
   - **Reload model files** UI queues the full source reload for the NEXT render-owner frame boundary, before `gfx::Device::beginFrame` and any `Renderer3D` draws; the previous Rebuild control continues to recompile cached layouts only.
   - Optional `--watch-model-files` and **Watch source files** UI poll filesystem parent/enabled child modification timestamps approximately once per second, ONLY on the owner thread; changes schedule a complete reload. Missing/non-filesystem file stamps are ignored. Each new timestamp schedules at most one attempt (no infinite failure retries). Explicit Reload can retry anytime.
   - Proper shutdown clears old Animator instances and appearance metadata before shutting down their native provider, and drains retained generations after definitive full GPU idle.
5. `4e92ec50dda789ddff6ccb86be293868b9e0d785` — same file:
   - If the final Device GPU idle proof fails, explicitly releases ownership of GPU-live Model pointers/providers into quarantine rather than returning and subsequently allowing the static Viewer destructor to free in-flight GPU resources. A provider shutdown failure similarly quarantines the owner. This is a last-resort fail-closed leak, NOT successful GPU retirement.
6. `61a9512d6d1ee7a2697f4f9b3048d95ce49faf49` — `docs/modelview-attachments.md`: updated user workflow describing file Reload vs cached Rebuild, CLI/UI watcher, compatible phase continuation, GPU fences, memory and actual test limitations.

## Native workflow

```sh
aiverse_modelview models/hero.glb@Walk \
  --attachments-file assets/hero.attachments.json \
  --watch-model-files
```

Or manually click **Reload model files** in the ModelView Attachments window, after editing a parent/child Model on disk. The parent uses its existing native `Animator` once per frame, then `ImportedAttachmentAuthoring::prepare(...HostPrecomputed)` sends the resolved pose and compiled socket attachments to generic View. View does NOT create another AnimationGraph instance or advance the host's clock.

`--watch-model-files` watches parent and enabled child paths on disk only (not virtual archive keys). The attachment layout JSON is still managed by the separate **Load/Save** authoring workflow. `Rebuild` compiles current cached assets, `Reload model files` replaces entire source/child CPU and GPU generations. Model position and compatible named clip phase survive; if a clip was removed, its actor safely falls back to bind pose.

## Resource lifetime and consistency guarantees / caveats

- Provider **A** stays live as old `RetiredNativeScene` after candidate provider **B** has published. Its old `Instance::Animator`, old appearance registry and old compiled attachments stay with A. GPU work that referenced A must finish before A can be destroyed.
- Source import/child staging never mutates A's resource cache. Candidate B can be discarded on failure only by deferring its GPU-owning provider until a safe full-idle cleanup. Existing GPU/texture rollback pools belong to each provider.
- ModelReadLeases remain short-lived and owned by a single presentation thread; `reimportModelFiles()` is called at the pre-Device-frame owner boundary and declines a swap when the old Model cache reports active readers. `Instance` references to new models are only committed when all parent files, poses, children and sockets succeeded.
- CPU `Animator` state migration copies only a **named clip and normalized phase**. It does not copy previous native skinning, montage, notifies, complex graph state, root motion or network gameplay. Rename/unavailable clip starts at bind/default.
- The GPU retirement cap of 8 is advisory; `ModelAssetProvider::shutdown` still uses an explicit full GPU idle barrier. At cap or shutdown, the viewer may stall; failed idle proof may retain/leak complete GPU owner generations. **Do not call this zero-stall or tokenized GPU shutdown.**
- The optional watcher is timestamp polling, not a filesystem event subscription or background thread, and does not recognize file contents changed without modification time. Failed uploads do not endlessly retry unchanged timestamps.
- No changes to any engine animation/ECS/retarget core, Model serialization schema, gameplay module or authoritative network data. Stage 157 modifies only ModelView example integration and documentation.

## Actual verification performed vs NOT RUN

**ACTUAL:** connected GitHub inspection of `apps/modelview/main.cpp`, `render::Animator` time/seek API, `ModelAssetProvider` staged load and resource cache safety contracts, prior Stage 156 handoff and PR #22 status. Five SHA-guarded app source commits and one user-guide documentation commit were made. GitHub source readback verified all helper, reimport, watcher, generation retention and single-writer code paths. PR was still OPEN/Draft/unmerged at pre-handoff code commit `4e92ec50dda789ddff6ccb86be293868b9e0d785`.

**NOT RUN:** CMake/native C++ compile/link, CTest, current HEAD green CI, actual launch/GUI or file watcher behavior, Metal/Vulkan/GLES GPU rendering, GPU fence fault injection, ASan/TSan/UBSan, memory/VRAM and high-crowd performance, `bash tools/animation/validate-final.sh`. Readback is NOT proof that the new C++ compiles or runs.

## Readiness table — Stage 157

| Subsystem | Source status | Remaining |
|---|---|---|
| Universal MotionWeave presentation ECS + graph | Implemented incrementally | Native workers/graph regression |
| Hot graph reload / portable named clocks | Implemented in core | Source/rig mismatch tests |
| Stable source logical ID / Model publication | Implemented in core | End-to-end source reuse tests |
| Animation whole-model LOD | Core implemented | Complex rigs and perf |
| Compiled retarget / morph mappings | Partial | Advanced importer mapping/GUI |
| Real imported child Models + compiled sockets | Core implemented | Visual lifetime/backend tests |
| ModelView JSON Attach/Detach and preview | Source implemented | Native compile/render validation |
| ModelView complete file hot-reimport | **Stage 157 source implemented** | Real GPU/file failure injection |
| ModelView optional source watcher | **Stage 157 source implemented** | Real watcher UI/filesystem behavior |
| Studio 3D attachment viewport | Stage 156 source implemented | Real Studio render/UX validation |
| Studio deferred GPU provider owners | Partial | True tokenized async shutdown |
| Scene/prefab attachment persistence | NOT IMPLEMENTED | Native scene/project serialization |
| Metal/Vulkan/GLES runtime | NOT VERIFIED | Real backend GPU tests |
| Complete CTest/ASan/TSan/benchmarks | NOT RUN | One final Mac pass after feature freeze |

"Implemented" means source committed and read from GitHub, not tested production behavior.

## Next Stage 158

1. Design and implement genuinely token-driven resource-owner retirement for independent imported Model sessions (source+child Models and shared textures) without mandatory full-idle on every provider release; work with concurrent graphics/asset owners, preserve a fallback when backend tokens are unavailable.
2. Complete generic morph/skeleton-retarget authoring, animated child source semantics, scene/prefab serialization, attachment transforms and richer Studio UX. Keep only ONE graph writer per distinct animation source and no genre imports in shared libraries.
3. Final feature freeze: integrate divergence with main/base in a disposable worktree, run the ONE comprehensive native Mac suite (CMake/compiler/CTest/Metal/Vulkan/GLES/sanitizers/fault injections/real renders/high crowd) and repair failures. No unreviewed merge/rebase or force push.
4. Continue an independent handoff + exact SHA/status table after every stage.


## Stage 157 source correction — independent standalone Model watcher

- `aa3f2300843610374fd3a393ded02f8d82d28624` — `apps/modelview/main.cpp`: `--watch-model-files` now activates the provider-backed native preview even when NO attachment JSON or `--attach` argument was given. Previously the watcher flag could remain inert outside an attachment preview. The same full provider+Animator/empty-socket staged reimport path applies; this remains source-only and NOT RUN.
