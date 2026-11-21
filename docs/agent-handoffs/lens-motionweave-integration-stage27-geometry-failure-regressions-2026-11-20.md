# AIVERSE LENS Stage 27 — GeometryScene transactional failure regression tests (2026-11-20)

## Branch and exact commits
- Branch: `feature/lens-motionweave-integration`, `main` unchanged.
- Implementation/test `724a28b45985798aae77d3174c6c3ba1cc06a3e1`: `tests/geometry/runtime.inc`, two new `TEST` cases exercising failed staged source/proxy loads and later success.
- Fixture correction `91bc512df8fda12a5750cfe5251cf54e202dfa6c`: repair raw JSON in the second test.
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage26-staged-geometry-2026-11-20.md`.

## New regression coverage (authored, not executed)
1. `geometry_transactional_reload_preserves_live_scene_on_source_failure`: install one loaded model as live scene. Attempt a replacement with two source files, successfully decode the first and inject an I/O failure on the second. Confirm error, original instance/group/proxy stats, no old-source retirement at failed staging, and that old content can still be submitted through Renderer3D. Retry with both valid files; expect two published instances.
2. `geometry_transactional_reload_preserves_hlod_after_proxy_source_failure`: install an HLOD scene with two original models. Attempt replacement whose regular source is available but explicit proxy source fails. Confirm exception and unchanged live HLOD instance/group/proxy counts, then retry successfully.
- Tests are appended to the *existing* `tests/geometry/runtime.inc` suite, so they use the established test harness rather than a new duplicate executable. No CMake registration changed.

## Architecture and compatibility
- No changes in runtime engine API, ECS components, serialization, or MotionWeave. Stage 26 `GeometryScene::loadTransactional` remains the production path.
- These cases exercise CPU/file-reader failure during staging. They are NOT GPU allocation failures. The GPU model upload failure, HLOD GPU buffer allocation failures, completion-token failure, and device-loss scenarios are still not covered.
- Tests use `entityfxtest::RecordingDevice` already used by the same geometry suite, without modifying the device backend or asserting device metrics unsupported by the fixture.

## Checks actually done
- Read current `GeometryScene.cpp/.h`, `NativePresentationRetirement.h`, prior Stage 26 handoff and existing `tests/geometry/runtime.inc`.
- Committed test sources and re-fetched them to verify both cases exist and JSON correction is reflected.
- CMake configure: **NOT RUN**.
- Native compiler: **NOT RUN**.
- CTest/geometry suite: **NOT RUN**.
- Metal/Vulkan/GLES runtime or backend compilation: **NOT RUN**.
- No passing test, performance, or pixel-parity claims are made.

## Risks and next steps
1. Actually compile and run the geometry suite, then adjust these tests for any existing harness/device resource lifecycle requirements.
2. Add a fake-RHI upload-fault device to exercise failure after some source/HLOD GPU resources are created; assert old scene remains visible, no invalid retirement, and clean recovery. Verify especially model and HLOD proxy allocations.
3. Improve Stage 26 failed-staging retirement ownership: the local `GeometryScene` currently drains best-effort, and with failed GPU idle may orphan pending model resources. Transfer or retain the staging retirement owner for retry instead of unbounded orphaning.
4. Complete staged publication of remaining GPU-dependent view owners (terrain, fog, roads, shores, overlays), then reconcile current MotionWeave branch via true merge. Perform one consolidated native CMake/CTest plus available Metal/Vulkan/GLES validation before `main`.
