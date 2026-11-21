# PRISMA Stage 39 — GPU-fenced Animation Graph/Rig preview and model viewer teardown

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous handoff:** `docs/handoffs/prisma-stage-38.md`.
**Main:** unchanged. No unrelated feature branches merged.

## Actual code changes with exact commits

1. `39fad94527157d499a62e7e67804bf356bbedfd2` — `apps/editor/animation/AnimationEditor.h`: introduce a complete `PreviewGpuBundle` owner holding the old loaded `render::Model`, compiled `AnimationGraphInstance` and `game::Portrait`. Detach them on model reload, preview reset, panel destruction or suspension and submit an owned callback to Editor's `GpuResourceRetirement::retireOwned(device, cleanup)`. Once the GPU fence truly completes, reset compiled graph before releasing its referenced model and the old Portrait. Never destroy model vertex/index buffers while the last GPU draw may still reference them. Public `suspendPreview()` retires GPU resources but leaves editor authoring state intact; `update` learns Editor's durable retirement owner.
2. `b0d7b303f27de05d38c03aa2f4281dfd2edda1b5` — `apps/editor/animation/AnimationEditor.h`: recompile restores prior numeric and vector animation parameters by matching names, without clobbering new graph parameters, after model/preview GPU resource recreation.
3. `6c50ebd7e06474d210d2de45d4d486e6563ac4f4` — `apps/editor/windows/AnimationGraphWindow.cpp`: suspend Animation Graph preview on hidden/closed dock tab, preserving graph document and controls. Recreate GPU assets when tab becomes active again.
4. `4c95f9b2a1acc61c1d729a47146b646caed49bc5` — `apps/editor/windows/AnimationRigWindow.cpp`: same GPU-lifecycle suspension for Rig editor, with no change to its ordered rig execution graph or JSON document.
5. `a17bae703223020ae7a2b7bf79c0c79bd750b6bf` — `apps/modelview/main.cpp`: standalone Gallery/ModelView app attempts a graphics submitted-work wait at shutdown before releasing imported models, the Gallery's Portrait, GUI and Renderer3D. Its GPU resources are owned by the viewer and do not participate in per-editor-tab lifetime.
6. `e3f98f0cf57f12fb2e6526c8e7114ab9cb6cf0bb` — `apps/editor/windows/ShaderGraphPreview.h`: repair Stage 37 Shader Graph preview GPU-cache-key code to use mutable `Json::itemsMut()` before removing layout-only node positions from the copied JSON. The previous const `items()` path could not legally mutate elements.
7. `1fc05ca2677712a83d6905cfd1f53d6f0155ed7c` — `apps/editor/animation/AnimationEditor.h`: add a GPU-independent Animation Graph preview key excluding visual node positions for the current graph and nested subgraphs. This prevents compile/reset of the animation graph on every node drag.
8. `0098c731f40d87a4fb09925f7496342535f35fd0` — `docs/prisma.md`: Stage 39 ownership, authoring state, modelviewer lifecycle, limitations and explicit unrun verification.
9. **This separate handoff** — `docs/handoffs/prisma-stage-39.md`, committed after the code/docs changes.

## Architecture, APIs, dependencies

**Animation/Rig GPU preview:** `AnimationEditorPanel::release()` now separates presentation GPU ownership from editor authoring data. It creates an independently owned `std::shared_ptr<PreviewGpuBundle>` with the old `Model`, `Portrait`, and moved `AnimationGraphInstance`. The owning Editor's `GpuResourceRetirement` captures this shared bundle in a copyable `std::function<void()>`. A current submission fence (`Device::gpuSubmissionFence()`) tags the retirement; `Editor::frame` processes it after the same device reports `gpuCompletedFence()`, or at the explicit pre-render `waitForSubmittedGpuWork` pressure drain.

The callback destroys in safe order: `AnimationGraphInstance::reset({})` (drops compiled graph pointer/reference to the Model), `game::Portrait::shutdown()` (Renderer3D and Portrait GPU targets), `render::releaseModel(*device,*model)` (native model GPU assets), then releases CPU owners. There is no second graphics submission or CPU frame-based fake fence. No ECS game-state mutation. On an unavailable generic owner, a verified GPU idle permits a direct safe cleanup; otherwise the whole bundle is stored inertly until process exit to avoid in-flight use-after-free. GPU devices must outlive normal callbacks.

**Per-editor authoring state:** `suspendPreview()` is called when Animation Graph/Rig is no longer the front dock tab or the tab closes. It can discard/recreate its GPU model, compiled animation graph and Portrait, but retains `doc_`, `saved_`, `dirty_`, undo/redo history, selected node, active subgraph/rig unit, camera/canvas position and zoom, `playing_`, yaw/ground height, `controls_` and pose snapshot. A rebuild restores matching numeric/vector parameters; newly introduced parameters use their graph defaults. The compiled graph's transient **runtime clock / transition states** are deliberately not carried across a model reload, as those states may hold pointers to the retired Model. This is a known limitation, not a claim of seamless live animation time transfer.

**UI-only edits:** `previewProgramKey()` copies the current graph and nested subgraphs, removes UI layout `position` fields in the copies, and derives the cache key only from operations and parameters. Dragging nodes while the GPU preview is active does not recompile/reset animation runtime. The ShaderGraphPreview source also gets an independent correction to mutable node iteration for its matching key policy.

**Standalone model viewer:** `aiverse_modelview` owns one Gallery/Portrait and its renderers until program shutdown; it now attempts `gfx::Device::waitForSubmittedGpuWork()` before destroying those resources. On a backend without that operation, the platform's original GPU-idle/device-teardown guarantee is still required. No new per-frame GPU wait was introduced in the standalone viewer.

## Source inspection / actual verification status

**Performed:** fetched and read `AnimationEditor.h`, the Animation Graph/Rig window wrappers, `AnimationGraphInstance`, `RtsPortrait`, standalone Gallery/ModelView app, Editor's GPU resource service and Json mutable APIs using the connected GitHub connector. Implemented changes and re-read affected source ranges to verify symbols, state-preserving paths and lifetime wiring. Corrected the previously authored ShaderGraphPreview JSON iteration.

**NOT RUN** — user explicitly prioritizes features before tests: native C++/Obj-C++ compiler, CMake configure, CTest, Metal/GL/GLES/Vulkan execution, actual UI screenshot/animation playback, GPU fence ordering, ModelView shutdown, memory profiling, CI. Source inspection is not a compiler result; no passing tests or production readiness claimed.

## Known limitations / next Stage 40

1. **Gameplay authoring state vs animation simulation:** draft JSON, controls and camera state survive GPU retirement; compiled animation playback clock, node transition state, contact solver and motion-matching state can restart when the preview is rebuilt. For seamless playback continuity, implement an explicit CPU-only graph snapshot/rebind protocol that validates the same model and graph structure before restoring state, not a blind transfer of Model-bound pointers.
2. ModelViewer's direct shutdown still requires graphics-device idle on platforms not supporting `waitForSubmittedGpuWork()`. It is not yet a general hot-reload/deferred-owner path; no update-loop live asset replacement was changed.
3. `apps/editor/animation/AnimationEditor.h` is a shared implementation used by both Graph and Rig windows, so changes affect both. Verify correct close, hide, reopen, asset swap, model load failure, undo/redo and uploaded geometry cleanup on actual editor builds.
4. The generic bundle queue retains unknown GPU bytes for Portrait, animation model textures, compiled graph and driver shader/pipeline allocations. Count-based pressure thresholds help but do not provide a hard VRAM cap. A future registry may track live GPU allocation bytes independently of owners.
5. Existing Stage 38 terminal-device-loss abandonment deliberately keeps complex bundles inert until process exit. Implement CPU-only detached cleanup only when all nested owners support it; never run `Device::destroy*` on a dead graphics device.
6. Other independent editor render paths (animation data inspectors outside the shared panel, external modelviewer Gallery, materials/TextureGen previews and custom plugins) may need the same explicit ownership policy; do not assume every RHI handle is intercepted by the queue.
7. **Stage 40 feature candidates:** introduce safe serialization/snapshot of presentation-only animation preview graph timing/parameters, migrate standalone Gallery to a durable per-device owner for live model/portrait asset reload, improve unified GPU-resource registry and per-view tracking, and expand CPU-only discard APIs for unrecoverable GPU loss. Work remains genre independent.
8. When the user authorizes tests, compile the editor and modelviewer, run CTest, verify parallel Graph/Rig tabs, continuous node dragging (no recompilation), model changes, undo/redo, hide/reopen, rendering after GPU completion and real GPU memory reclamation in Metal/GL/Vulkan. Record actual PASS/FAIL and fix failures.
9. After Stage 40, create its own `docs/handoffs/prisma-stage-40.md` with exact SHAs, architecture, actual tests and next concrete action. Do not merge PR #26 into main without explicit user direction.
