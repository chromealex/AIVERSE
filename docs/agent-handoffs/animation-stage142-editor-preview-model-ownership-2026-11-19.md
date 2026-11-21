# AIVERSE MotionWeave / Universal View — Stage 142 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 141](animation-stage141-explicit-frame-boundary-preview-lifetime-2026-11-19.md)

## Feature-first workflow

The user explicitly requested implementing remaining MotionWeave / Universal View functionality **before a single comprehensive native validation pass**. Stage 142 implements editor features and records source-level evidence only. No CMake configure/build, CTest, macOS Metal/Vulkan/GL real GPU, sanitizer, screenshots, benchmarks or passing CI was executed or observed for these commits. Do not mark this code tested or ready for `main`. The future one-command entry point remains `tools/animation/validate-final.sh`, to be run after an honest feature-freeze gate.

## Branch topology

Started from Stage 141 HEAD `8780a3cb437bd369df0a69f2bd8c15393151b38d`.
Last code head before writing this handoff: `49f994e2bd97c43b68c264e8a827133d22839001`.

GitHub comparisons at code readback:
- Relative to `main`: feature ahead **1080**, behind **305** (DIVERGED; merge base `58e29ab052458fffcdebebffe27022b53adb9367`).
- Relative to PR #22 base `feature/universal-entity-inspector-fps`: feature ahead **1450**, behind **615** (DIVERGED; merge base `c944882193c1404a7e47f409f8d7847c766b4e31`).

Do not rebase/force-push this shared animation branch, auto-merge main or PR base, change PR state, or alter another agent's renderer/texture/PHYSIS/TerraWeave/SpectraFX branch. Integrate overlap in a separate disposable worktree after owner coordination and native validation.

## Exact source commits

1. `f6b2f5dbb69ea6c929aca8a315eb13d734ae1cc2` — `apps/editor/Editor.h`: added a generic `render::NativePresentationRetirement` owned by the Editor, public `retirePreviewModel(std::unique_ptr<Model>&) noexcept`, and `pendingPreviewRetirements()`.
2. `edc3f4b3a4d3a6db69764d0a3d28fdc750e104ec` — `apps/editor/Editor.cpp`: implementation of preview Model retirement, collection in the established **Editor::frame pre-render Device owner hook**, explicit GPU idle proof before window teardown, and final retirement queue drain after `windows_.clear()` before other GPU owners are destroyed.
3. `d0fc330d06974eed90cdb7b385db6d61e808ef99` — `apps/editor/windows/ModelingWindow.cpp`: stage a new CPU Model, AnimationGraphInstance and bind pose before touching the current preview; failed graph compilation preserves last-good; on GPU upload error, enqueue the partially uploaded Model; on success drop graph references, enqueue old GPU Model and install new graph/Model/pose. The window destructor now enqueues Models through Editor rather than releasing them immediately (with a guarded GPU-idle-only fallback if no Editor owner exists). Project switches also enqueue.
4. `260ef885d7c4d3cd069d4133058eb8e25f503814` — `apps/editor/animation/AnimationEditor.h`: `AnimationEditorPanel` owns its preview CPU graph but asks Editor to retire the GPU Model when resetting/switching projects. The offscreen Portrait render target is **reused** between graph/model reloads, initialized only once on demand, and shutdown only on panel destruction (after normal Editor GPU shutdown idle proof). A rejected/throwing partially uploaded preview Model is retired; the `modelKey_` is set only after a completed upload and portrait availability. Compile key `programKey_` is committed only after compilation succeeds, allowing retry on failure.
5. `6264414ac32d5559531ceb90f001eea6c93d3584` — `ModelingWindow.cpp`: when an ImGui tab closes but the EditorWindow object remains for MCP/agent operations, drop graph/pose references, queue the GPU Model and call `Viewport::release()` through Editor's offscreen target pool; retain window registration for later reopening.
6. `1f3b0b4f655e2ee6b86d597ac46a17fc9d993c17` — `AnimationEditor.h`: exposes `suspendPreview()` to queue the model/clear graph for an inactive tab without tearing down its reusable Portrait target.
7. `7c9206f83475819e09f1ce3c6fb475c93b7c3600` — `apps/editor/windows/AnimationGraphWindow.cpp`: request preview suspension when its tab is closed.
8. `49f994e2bd97c43b68c264e8a827133d22839001` — `apps/editor/windows/AnimationRigWindow.cpp`: same inactive-tab suspension for rig preview.

## GPU lifetime/API contract

- A dockable editor window may be drawn during an Editor GPU frame that also contains 3D Scene/Game and offscreen passes. A successful Model upload can still be referenced by queued GPU draws **after the window returns**. Calling `releaseModel` in an editor window's reload, project-change, close or destructor path is therefore unsafe.
- `Editor::retirePreviewModel` removes the `unique_ptr<Model>` from active editing state and moves ownership to the generic **ordered GPU-token retirement queue** already introduced in Stage 140. If no Device exists it explicitly relinquishes ownership rather than triggering unsafe GPU destruction.
- Editor's established `Editor::frame` pre-render hook polls `previewModelRetirement_.collect(device)` only when resources are pending; this is **once per Editor frame**, before `windows_->update/draw`, not in a ModelingWindow or Portrait draw callback. It does not create a separate MotionWeave update/graph writer.
- `Editor::shutdown` proves a successful `gfx::Device::waitForResourceIdle()` **before destroying any dock window**. Window destructors may queue Models; Editor then calls `completeForShutdown` on the native retirement queue, requiring another proof and draining tokens before it destroys other RHI owners or retires the Device epoch. On an unprovable barrier, shutdown throws and does not silently free the previews.
- `ModelingWindow` stages graph compilation against the **new Model** before the GPU upload and old-model detach; an invalid authoring update does not erase the last-good GPU preview. The graph remains a single local preview writer and cannot own an authoritative gameplay tick.
- `AnimationEditorPanel` retains one Portrait render target across graph/model reload and project switch; the Model may be replaced safely without repeatedly calling `Portrait::shutdown` and blocking GPU idle every edit. Partial Portrait target allocation failure uses its existing full-idle shutdown path and returns an error. The final Portrait shutdown remains tied to Editor window destruction.
- Closing an ImGui tab does not destroy the window object; Stage 142 explicitly releases the active GPU Model for Modeling, AnimationGraph and AnimationRig windows while preserving the registry/editor state needed to reopen.

## Code review performed (source-only, no tests)

Actually inspected `EditorWindow`, `Editor::frame`, `Editor::shutdown`, `Viewport`, `NativePresentationRetirement`, `ModelingWindow`, `AnimationEditorPanel`, the graph and rig wrapper windows, `AnimationGraphInstance` move API, `Portrait::init/shutdown/renderPose`, and Stage 141 handoff. Committed SHA-guarded changes and read back all touched implementation files, verifying the preview queue, owner pre-frame poll, no direct Model release in the changed Modeling/Animation preview functions, staged compilation and closed-tab suspension. Queried GitHub branch/main/PR ancestry and older Action statuses.

**NOT RUN** native compiler/CMake/CTest, test execution or CI pass for the new SHA, real Metal/GL/Vulkan/portrait screenshot, offscreen Model hot reload under active GPU work, sanitizers or performance. Source inspection is NOT proof that editor frame GPU semantics or all UI paths are correct.

## Outstanding risks / not yet a feature freeze

- The editor also owns other GPU resources outside these two Model panels (Modeling renderer pipelines, viewport target pool, Portrait renderer/targets). Stage 142 uses a single full Device idle proof at **Editor shutdown**, plus existing offscreen pool for ordinary viewport target changes; it does **not** add a new per-frame retirement queue for every pipeline and texture. Device-loss/failure may intentionally leave resources unreleased.
- A few presentation/portrait APIs still directly allocate shared/kit textures. These were not broadly changed to avoid crossing concurrent renderer/texture ownership. Verify actual GPU-safe disposal during final hardware pass.
- `AnimationEditorPanel` keeps its Portrait render target allocated while its tab is closed for fast reopening; it queues the Model but does not claim a fully unloaded editor window. Final target release occurs during window destruction.
- The existing Editor frame hook must be the *true Device owner pre-render boundary*. It is sourced from the same `Editor::frame` path already responsible for View, image rollback, offscreen targets and renderer tokens; the C++ call order is verified, real graphics timing is not.
- There may be direct `ModelResourceCache` or `ModelAssetProvider` callers that retained `noexcept` wrappers or unowned raw pointers; continue auditing.
- **Next functional gaps:** GeometryScene's direct Model GPU release and WorldScene streaming lifecycle, editor preview owner/graph consistency under source hot reload, remaining Universal View/legacy RtsView adapters, native FPS/RTS/editor parity, model/LOD/retarget continuity and Stage 137 feature-closure gate. A large and divergent PR cannot be marked feature-complete yet.

## Next Stage 143 (feature-only)

1. Audit and fix `engine/render/GeometryScene.cpp` GPU release during `loadImpl`/proxy rebuild, including rejection/exception rollback and runtime viewport references; coordinate with geometry/rendering owners instead of overwriting their peer branch work.
2. Audit `engine/world/WorldScene.cpp` streaming cell eviction and shared Model ownership, especially direct `releaseModel` for resources referenced by live GPU submissions.
3. Continue native editor and FPS/RTS/Universal View host adoption while preserving MotionWeave presentation-only graph authority.
4. After each bounded feature stage, append an independent dated handoff, `AGENTS.md`, `docs/animation-graphs.md` and universal journal. **No tests before finishing all features** by user's instruction. After actual feature freeze, perform the complete `tools/animation/validate-final.sh` Mac native pass and real GPU/visual parity. Keep PR #22 Draft and main untouched until validated.
