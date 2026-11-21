# AIVERSE MotionWeave / Universal View — Stage 141 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 140](animation-stage140-native-scene-gpu-retirement-2026-11-19.md).

## Execution policy

The user requested **finish all MotionWeave / Universal View features before one comprehensive native test pass**. This is a feature-only stage. Source edits and GitHub readback were performed; **NO CMake, compiler, CTest, CI validation, macOS Metal/GL/Vulkan, sanitizers, screenshot tests or benchmarks were run**. Do not claim that Stage 141 compiles, passes tests, or makes the animation PR merge-ready.

## Branch and commits

Continued from Stage 140 head `3c6f8b82502dcf1c643b43eadc8f3ed6dd6e40af`.

Exact implementation commits:
1. `8468533a79db21b52a374b7d3eda6585de0f9221` — `modules/rts/view/RtsView.h`: declare unified owner-frame retirement APIs out-of-line (road/shore types are incomplete in the header).
2. `6cf0a376ed76befc88ba880e57c9c1137b536197` — `RtsView.cpp`: aggregate road/shore resources into existing pre-render `pendingModelRetirements`/`collectRetiredModels` hooks; remove unsafe automatic retirement from `RtsView::draw`.
3. `8b4bd189ea1ee8cb540542857ef34c1cf62df459` — `engine/render/ModelAssetProvider.h`: remove `noexcept` from `find` and `publicationBackpressured` wrappers, as underlying Stage 134–135 Model cache owner checks can throw `std::logic_error`.
4. `e69d744557cdf2a9abba483dc232455fc73284d2` — `apps/modelview/Gallery.h`: expose a single explicit frame-boundary GPU retirement hook.
5. `8c656f8d39e10f2d3eca53597b66d63a6f802bf4` — `Gallery.cpp`: poll both Model/rolled-image and paint GPU queues once before offscreen portrait and main gallery draws.
6. `063e6479ba80116048b4fc169377dc834cd721e4` — `apps/modelview/main.cpp`: poll Gallery and generic ModelAssetProvider GPU retirements immediately before `gfx::Device::beginFrame`, not during two scene/portrait submissions in a single frame.
7. `509786bb6de14627b768ef6739c1537dcb1eb1ba` — `RtsRoadView.cpp`: propagate failed GPU idle shutdown proof to the parent rather than silently returning and allowing further teardown.
8. `861ffbadb9d8555e609752658ea5abb7811b66c2` — `RtsShoreView.cpp`: same fail-closed shutdown propagation.
9. `0f7ca275d58e74f3c917fcf50cac9a65bbaea5ae` — `apps/modelview/main.cpp`: require full GPU idle at explicit viewer shutdown before destroying raw Models, native asset owner, portrait, GUI, ground and renderer allocations. If backend cannot prove idle, skip destructive calls and report warning.
10. `64e30ec14b5026d658e8a1ff69924d9fb36e8d05` — `apps/modelview/Gallery.cpp`: require real GPU completion before destroying the offscreen portrait texture from an independent Gallery caller.
11. `a56e76e1d2c4bbc57c03b8cfaf0763b2b8cea239` — `modules/rts/view/RtsView.cpp`: verify a full-device GPU idle barrier BEFORE `GeometryScene::shutdown`, which previously ran before the existing ModelResourceCache shutdown barrier.

At last source readback, code HEAD `a56e76e1d2c4bbc57c03b8cfaf0763b2b8cea239`.
Latest GitHub comparison: feature **ahead 1067 / behind 305** vs current `main` (DIVERGED), and **ahead 1437 / behind 615** vs PR #22 base `feature/universal-entity-inspector-fps` (DIVERGED). Do not rebase/force-push/merge other agents' branches or change `main` until reviewed integration worktree, ownership coordination and native validation.

## Critical correction: RtsView::draw is NOT an authoritative frame boundary

Stage 140 polled road/shore `NativePresentationRetirement` queues at the start of `RtsView::draw`. That can be invoked multiple times in a single gfx Device frame, for example editor scene/game view and modelview Gallery + offscreen portrait. The call occurs during View scene assembly, which is NOT sufficient proof that previous GPU submissions are complete or that creating a new native retirement token is legal.

The existing `GameClient::frame` and `Editor::frame` already invoke the canonical owner hook, `view_.collectRetiredModels()`, before recording new frame view passes. Stage 141 extends the same hook rather than introducing a second competing retirement clock.

`RtsView::pendingModelRetirements()` now counts pending `ModelResourceCache` Models **PLUS** road and shore `NativePresentationRetirement` entries (including SpriteMesh buffers). This preserves existing host call sites that poll only when that count or rolled-back images is nonzero. The name remains for ABI/source compatibility but also includes standalone buffers. It is no longer `noexcept` because the generic cache's owner-validated retirement statistics may throw on a foreign thread.

`RtsView::collectRetiredModels()` explicitly polls four independent systems:
- its own `ModelResourceCache`;
- the staged image rollback pool;
- the road native retirement queue;
- the shore native retirement queue.

All four are polled, even if any one is pending, and result returns true only if every queue is empty/completed. Only the host should call this from a proven pre-submission Device owner frame hook. `RtsView::draw` no longer generates GPU retirement markers or waits for idle.

## Modelview / offscreen gallery integration

Modelview's `Viewer::frame` explicitly calls `Gallery::collectRetiredGpuResources()` and, if necessary, the independent `nativeModels.collectRetired(device)` **before** `gfx::Device::beginFrame`. Gallery polls Model/rolled-image and canvas paint queues. It may then render an offscreen portrait and a main gallery in the same GPU frame without multiple retirement passes.

The same GameClient/Editor pre-render hooks already present in the repository automatically discover road/shore pending resources through the augmented `pendingModelRetirements` count. No separate editor or client global dispatch was necessary.

## Shutdown GPU lifetime fixes

- `RtsRoadView::shutdown` and `RtsShoreView::shutdown` must **throw** when `completeForShutdown` cannot prove GPU idle, keeping resources alive and aborting parent teardown rather than returning silently.
- `RtsView::shutdown` now proves GPU idle before releasing GeometryScene, Model cache and scene-owned objects. The prior order freed standalone GeometryScene Models before the later ModelResourceCache GPU idle barrier.
- Raw modelview and Gallery portrait shutdown each require successful full GPU idle before destroying native assets. An unproven barrier stops native release; it does not prove device recovery or leak-free shutdown.
- `ModelAssetProvider::find` and `publicationBackpressured` now preserve exceptions thrown by cache owner checks instead of declaring `noexcept` and causing `std::terminate` on wrong-thread accesses.

## Explicit limitations and remaining work

- Stage 140 `NativePresentationRetirement` still stores a per-host ordered token queue and a 128-resource count threshold for unsupported token backends. This is not a VRAM quota and could retain a few very large allocations until shutdown.
- The upstream gfx::Device must still guarantee the correct frame-boundary semantics of `beginResourceRetirement`, `resourceRetirementComplete`, and `waitForResourceIdle`. This source audit establishes a clearer host API, **not hardware-proof correctness**. Multiple windows or a platform that renders outside client/editor/modelview frame hooks must call the explicit collector before its first pass.
- Full-device idle at shutdown may block and is acceptable only on teardown, not as a per-frame workaround.
- Stage 140 retirement covers road/shore Models + standalone SpriteMesh buffers, while long-lived kit textures, paint cache and SpriteBatch pipelines remain active until verified shutdown. Handle other GPU objects through their own owner.
- **Outstanding feature owners:** `apps/editor/windows/ModelingWindow.cpp` directly calls `uploadModel` and `releaseModel` for preview models, including in destructor/update; `apps/editor/animation/AnimationEditor.h` owns a separate preview Model and portrait with direct release. `engine/render/GeometryScene.cpp` uses direct Models with GPU releases during `loadImpl` and `shutdown`; `engine/world/WorldScene.cpp` has streaming chunk releases. Those hosts need a separately scoped, correctly ordered retirement and failed-upload policy rather than blind edits to other agents' modules. The generic `ModelAssetProvider` paths already use `ModelStagingTransaction` and a dedicated rollback queue, so not all providers need migration.
- Full Universal View extraction, MotionWeave FPS/RTS/editor graphical equivalence, live model/retarget/LOD source hot reload and the remaining Stage 137 feature-freeze checklist remain OPEN. Do not claim final functional completion.

## Actual work and validation state

**ACTUALLY PERFORMED:** connected GitHub source reads of `GameClient::frame`, `Editor::frame`, Gallery portrait/scene update, ModelResourceCache/ModelAssetProvider ownership, RtsView scene draw/shutdown, direct editor model previews and GeometryScene; SHA-guarded edits to feature branch; readback confirming unified collector, no retirement from `RtsView::draw`, preview frame hook, and pre-destruction barriers; current branch/main/PR base comparisons.

**NOT RUN:** any native CMake configure/compile/link, CTest, CI green for Stage 141, actual GPU token/fence-failure injection, Metal/GL/Vulkan visual parity, ModelView Gallery offscreen screenshots, FPS/RTS/editor image parity, sanitizers, benchmarks or full `tools/animation/validate-final.sh`. All these checks remain deferred until genuine feature freeze by user request.

## Next Stage 142 — feature-only

1. Close the direct Editor ModelingWindow/AnimationEditor preview Model GPU ownership on revision change, project change, device teardown, preview portrait release and failed upload. Use a generic owner-scoped deferred retirement or the existing app renderer resource queue. Avoid blocking GPU idle on every preview edit.
2. Review GeometryScene's `loadImpl` and WorldScene stream teardown for shared/cached resources, coordinating with the graphics/geometry owners; avoid changing unrelated peer branches.
3. Finish remaining Universal View and MotionWeave host adapters with exact source-generic presentation continuity, retarget/LOD, and no duplicate graph writer.
4. Continue separate handoffs and docs after each stage, still no tests until functional freeze. After that, run `bash tools/animation/validate-final.sh` in one comprehensive native Mac pass plus genuine GPU/visual checks.
