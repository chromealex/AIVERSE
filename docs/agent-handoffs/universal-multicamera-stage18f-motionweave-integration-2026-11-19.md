# AIVERSE LENS — animation integration checkpoint / VisualFrame Stage 18F (2026-11-19)

## State
- LENS branch: `feature/universal-multicamera-render-targets`, integration PR: https://github.com/chromealex/AIVERSE/pull/49
- Animation source branch: `feature/universal-ecs-animation-pipeline`.
- PR #49 is **draft and non-mergeable** according to GitHub at creation; this is **NOT a completed merge**.
- Last reported animation source commit at PR creation: `a8ce597b2e4cf1344d55bcadbf0b36f2e7b9899b`; refresh both refs before reconciliation, both branches have concurrent agents.
- New independent file: `engine/render/CameraVisualFrame.h` implementation `cc5b9c92eeba8b7e51fc943bf687f3488dd14fd2`, include fix `0aef3da899ed506d2fb94ea1cb520fa068bb79f9`.

## Architecture
- `CameraVisualFrameId` identifies the immutable prepared scene frame by simulation tick, presentation frame and absolute seconds.
- `CameraVisualFrame<PreparedFrame,SubmitFn>` wraps a const reference to a frame object and a submit callback. `submit(view,renderer)` can be invoked for multiple cameras with the same immutable frame.
- `makeCameraVisualFrame` factory deduces the callback type.
- IMPORTANT: callback read-only semantics are currently contractual only. The template does not automatically freeze outside state, prevent a lambda mutating shared graphics state, or guarantee asset lifetime. It is not yet wired to MotionWeave/RtsView.
- The existing `RtsView::drawFrozenModels` in LENS predates MotionWeave's generic View adapter and must be rebased/reimplemented without overwriting the animation branch.

## Integration decisions
- Animation branch is source of truth for `modules/rts/view/RtsView.h`, `RtsView.cpp`, `RtsViewDraw.cpp`, `RtsAnimate.cpp`, including generic `aiverse::View`, `View::submitModel`, attachment composition, appearance registry, animation graph lifecycle and source reload behavior.
- Preserve LENS-owned scheduler, ECS camera components, capture graph, RenderTargetPool, CameraRuntimeHost, CameraClientAdapter, Vulkan/Metal/GLES fixes, Studio camera window, tests/handoffs, and GameClient opt-in RT path.
- Integrate a new immutable render-packet snapshot from MotionWeave output after the single animation update. Per-camera culling and LOD should be view-specific, but animation pose updates and gameplay/presentation events must not run again for every camera.
- Pay special attention to model/material/pose pointer lifetime in proposed packet snapshots. Stable immutable source identity and GPU in-flight retirement are required.
- A direct GitHub merge currently reports conflicts (289-file PR); never replace whole RtsView/CMake source files from LENS without three-way reconciliation.
- Do not merge into main until both source branches are integrated, compilable and tested.

## Validation
- Remote GitHub read/write only. **NOT RUN:** git three-way merge locally, CMake, compiler, CTest, GPU tests, runtime validation.
- GitHub connection supports PR creation, source file edits and git-object APIs, but the branch merge is not yet resolved. No claim of completed animation import.
- Build validation after conflicts: at least editor+client+runtime, animation fixtures, camera fixtures, Metal GPU and Vulkan/GLES build-only. Record test results verbatim.

## Next agent actions
1. Check fresh branch heads; use a local checkout with Git credentials or a GitHub merge mechanism that can reconcile conflicts, preserving both branches. Review file-by-file conflict hunks in RtsView/RtsAnimate/RtsViewDraw and CMake.
2. Wire frozen MotionWeave packet snapshot into CameraVisualFrame, replace the temporary LENS drawFrozenModels, verify main camera and additional cameras read one animation evaluation without consuming events twice.
3. Add CPU tests for one animation update + N camera renders and event counts; validate memory lifetime and cache behavior.
4. Only after code integration, run consolidated build/tests and document failures/fixes.
