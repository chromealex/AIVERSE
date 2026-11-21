# AIVERSE MotionWeave / Universal View — Stage 156 handoff

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN/DRAFT (not merged)
Previous: [Stage 155](animation-stage155-studio-attachment-staged-reimport-2026-11-20.md)

## Goal and hard architecture boundary

Implement an actual **Studio 3D preview** of parent and imported child Models from the new AttachmentAuthoringWindow's OWN resource owner, and avoid a mandatory full GPU-idle stall on every successful Studio attachment reimport. The universal `MotionWeave`, reusable `aiverse::View`, source model readers, LOD and socket helpers remain **strictly independent of FPS, RTS, RPG, editors and other game genres**. The Studio window only adapts its authored files to these existing APIs.

The user's instructions persist: **finish features, then run ONE comprehensive native Mac test pass**, and **show a readiness table after every implementation stage**. All claims in this handoff describe committed source, not proven compiler/renderer/runtime behavior.

## Exact implementation commits

1. `ff2d5e7d137343fe633398e628732a0a91154216` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`:
   - Uses the established `Editor::Viewport`, `Renderer3D`, `RenderDeviceLifetime::Lease`, and editor-managed `OffscreenTargetRetirement` to render a TRUE offscreen parent+child model preview inside the generic Studio attachments authoring tab.
   - Camera frames the imported parent rig bounds, supports orbit dragging, selects the first imported clip when available, has a Play checkbox and phase slider. A temporary `render::Pose` is sampled from the published parent Model and turned into world-space bone matrices without creating a second `MotionWeave` ECS owner.
   - Calls the existing generic `ImportedAttachmentAuthoring::prepare(...AnimationOwner::HostPrecomputed)`, retains both parent and child read leases while calling `View::submitOne` and `Renderer3D::render(pass)`, binds the parent pose/node matrices, and draws compiled socket attachments against the exact published Model epoch. This is a separate **authoring preview** with its own ModelAssetProvider; ModelingWindow and AnimationGraph previews remain separate owners and are never borrowed.
   - Preview viewport/renderer are torn down after GPU-idle proof at Studio window destruction; the Editor's existing shutdown already requires an idle proof before clearing windows. The Studio's common GPU lifetime and offscreen-target queue manage backend target lifetime.

2. `9a3f5cd17866455dcd5b0bfa86089efa63163335` — same Studio window:
   - Adds **bounded deferred resource-owner retirement**. A new complete source+child candidate ModelAssetProvider is published as soon as file import, upload and socket compilation succeed. The previous session moves into a pending retired-provider vector, remaining alive so outstanding GPU submissions cannot outlive its Model resources.
   - Failed scratch imports containing partially uploaded GPU models are also transferred to this deferred owner queue rather than immediately destroyed. Project changes move old sessions to the same queue.
   - A configurable-in-code cap of **8 pending provider sessions** triggers a full idle barrier during the Studio owner `update` before dropping the accumulated sessions. Closing the editor likewise requires a full idle proof. An unavailable GPU completion proof leaves providers alive, and an allocation failure in the deferred queue fails closed by keeping the affected provider alive rather than destroying unproven resources.
   - This is **deferred ownership** with a potential full-idle fallback (and a possible resource leak on fatal GPU proof failure), NOT a general per-provider GPU token retirement implementation or a guaranteed zero-stall hot reload. `ModelAssetProvider::shutdown` presently mandates `Device::waitForResourceIdle`; a real fully nonblocking implementation needs a new provider GPU-safe shutdown/release contract and per-provider token tracking.

## Source paths / API shape

- Modified source: `apps/editor/windows/AttachmentAuthoringWindow.cpp` only.
- The shared code `engine/view/ImportedAttachmentAuthoring.h`, `engine/view/View.h`, `ModelResourceCache` and `ModelAssetProvider` remain unchanged. The View is always a **render-only consumer of a precomputed pose** in this Studio preview: it does not run an extra graph player or mutate gameplay ECS state.
- The class now owns `render::Renderer3D renderer_`, `Viewport viewport_`, `::aiverse::View presentationView_`, transient preview pose/matrices, camera/time controls, plus `retiredSessions_`.
- Call `stageAndPublish` from the next pre-draw Studio `EditorWindow::update` as in Stage 155, not from the ImGui button callback. The old session is retained until a proven completion/retirement boundary.
- The Studio native Model/Graph editor's direct `unique_ptr<Model>` previews are NEVER passed into this separate provider's cache. All source and child Models are imported anew from actual project-relative asset files.
- A split between CPU ModelReadLease and GPU device completion remains mandatory: the former protects CPU pointers while preparing/submitting and is **not** a GPU fence.

## Important remaining limitations and risks

- The new viewport's animation preview currently uses the first imported Model clip (or bind pose), not an editor-authored full AnimationGraph, IK/ragdoll or multiple montage tracks. Full graph parity, selection among clips, advanced morph and timeline UI are later features.
- Full GPU token-based, per-session nonblocking retirement is **NOT IMPLEMENTED**. The bounded deferred queue can still stall on its threshold, and may retain multiple complete model/texture caches simultaneously. Do NOT describe the algorithm as zero-stall or say a successful GPU token was observed.
- `aiverse_modelview` still does not have a safe full **on-disk model-file reimport** transaction across its parent Animator, child ModelProvider and published cache. Its `Rebuild` remains a layout-recompile against resident Models. Do not invalidate its owner cache while Animator keeps a previous Model pointer. This is Stage 157 work.
- Studio still has no drag-and-drop/gizmo transforms, authored prefab/scene writeback, attached animated-child graphs, advanced skeleton retarget authoring and nonblocking preview capture. GPU backend shape, exact camera framing, material/texture parity and model target lifetime require real native testing.
- A full `ModelAssetProvider::shutdown` currently waits GPU idle even after a previous successful barrier. Optimizing provider-level retirement is separate, nontrivial graphics architecture work and cannot be claimed as complete by simply deferring a `unique_ptr`.

## Checks actually performed, versus NOT RUN

**ACTUAL:** read Stage 155 handoff and current connected GitHub sources; verified Studio ModelingWindow's existing `Viewport::ensure/image`, `Renderer3D::beginScene/render`, GPU offscreen target lease usage, generic `ImportedAttachmentAuthoring::prepare`, device lifetime, `ModelAssetProvider::shutdown`, `ModelResourceCache::shutdown` and the native GPU-fenced `AsyncResourceRetirementQueue`. Submitted two SHA-guarded GitHub source edits. Code has been read from the feature branch.

**NOT RUN:** CMake configuration, native C++ compiler/link, CTest, fresh green CI for this HEAD, Studio GUI launch, actual parent+child pixels, real Metal/Vulkan/GLES/GPU fences, failure injection, ASan/TSan/UBSan, CPU and GPU memory benchmarks, single comprehensive `bash tools/animation/validate-final.sh`. Until these pass, Stage 156 remains a *source implementation*.

## Required readiness matrix at Stage 156

| Subsystem | Status | Still required |
|---|---|---|
| MotionWeave universal ECS and AnimationGraph | Implemented source | Native worker and rendering verification |
| Graph hot reload / name-based portable phases | Implemented source | Source-epoch/rig mismatch tests |
| Logical source identity / Model epochs | Implemented source | Real cross-host validation |
| Animation LOD and compiled retarget | Partial | Complex rigs and large-crowd memory/perf |
| Bone sockets / static imported attachments | Core source exists | Visual backend parity and child import tests |
| Studio Attach/Detach/JSON authoring | Source exists | Real Studio interaction and saved-layout tests |
| Studio embedded parent+child 3D viewport | Stage 156 source implemented | Compile, visual screenshots, GPU target lifetime |
| Studio deferred old-resource ownership | Stage 156 source implemented | Tokenized retirement, cap/idle failure and VRAM tests |
| ModelView file hot reimport | Not implemented | Staged parent Animator + child cache swap |
| Advanced retarget/morph/rig graph authoring | Partial | Studio UI and complex imported rigs |
| Prefab / live scene attachments | Not implemented | Scene/serialization integration |
| Metal, Vulkan, GLES | Not verified | Real backend GPU tests |
| CMake, CTest, ASan/TSan, benchmark | NOT RUN | One comprehensive final Mac validation |

## Next Stage 157

1. Implement safe **ModelView on-disk reimport** with a complete staged provider/cache and rebind Animator/pose/compiled child attachments atomically. Preserve last-good visible models on failed import. Retire the old provider only when GPU work is proven complete.
2. Design a genuinely GPU-token-based provider retirement owner that does not call full GPU idle in every `ModelAssetProvider::shutdown`. Respect texture sharing, cache read lease counts and device generation, and carefully coordinate with the renderer/asset agent before invasive edits.
3. Extend the Studio attachment preview to select authored clips, support rig/morph maps, animated child sources and reusable scene/prefab authoring, still with a single animation writer.
4. After completing all features, run exactly the comprehensive native Mac CMake/CTest+Metal/Vulkan/GLES+sanitizer/visual/perf suite once and then fix defects. Keep Draft PR #22 until verified, do not auto-merge/rebase/force push the concurrent branches.
5. Each new stage requires its own independent handoff, source SHA list, honest test evidence and a **user-visible progress table**.
