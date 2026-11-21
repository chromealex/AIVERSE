# AIVERSE LENS Stage 28 — GPU allocation fault tests for GeometryScene (2026-11-20)

## Branch and implementation commits
- Repository: `chromealex/AIVERSE`. Branch: `feature/lens-motionweave-integration`; `main` not changed.
- `3c9bb219d4189c6ab6180d6aabe3c4b0d05fb9c6`: `tests/tests.cpp`, allow the existing `entityfxtest::RecordingDevice` to be subclassed (remove `final`, no production API changes).
- `898006415a7f30a8309467403cbe4a704dcf920f`: `tests/geometry/runtime.inc`, add `GpuUploadFailDevice` and two GPU buffer-allocation fault scenarios.
- `0d375a0f138df44f800f047ac6197f668da99ed9`: qualify base `entityfxtest::RecordingDevice::createBuffer`.
- `2ff169aa60b177e5424269ac039078a59d47cb2e`: inject source-model allocation failure after the first model's expected vertex/index uploads to cover a partially prepared staging scene.

## What is tested in source (not executed)
- `geometry_transactional_gpu_source_upload_failure_preserves_live`: install an initial static scene, stage two replacement sources and reject the third subsequent buffer allocation (expected: second source's vertex buffer). Assert that `loadTransactional` throws, original `GeometryScene::Stats` remain unchanged, live retirement queue is not populated by the failed candidate, then retry with allocation fault disabled.
- `geometry_transactional_gpu_hlod_proxy_upload_failure_preserves_live`: enable HLOD for two new sources; inject failure at the first staging GPU buffer allocation (expected: HLOD proxy if proxy is generated). Assert rejection, stable original stats and retry with fault disabled.
- Mock `GpuUploadFailDevice` provides a successful idle barrier for controlled staged GPU cleanup. It does NOT model real asynchronous GPU memory, and requires native verification of HLOD proxy allocation order.
- Tests were appended to existing `tests/geometry/runtime.inc`, already included from `tests/tests.cpp`. No separate CMake target created.
- No ECS, animation, camera ABI or production renderer code changed in this bounded stage.

## Verification actually performed
- Read `tests/tests.cpp`, `tests/geometry/runtime.inc`, `GeometryScene.h/.cpp`, prior Stage 27 handoff and inspected current feature branch.
- Successfully committed and re-fetched the source changes; verified both new test names, mock device override, and source-fault injection setting.
- **NOT RUN:** CMake configure, native C++ compile, CTest, GPU fault-injection tests, Metal/Vulkan/GLES builds, pixel parity and CPU/GPU profiling.
- These tests are authored but not proven to compile or pass.

## Known issues and next exact work
1. Run native `aiverse_tests`/CTest once the user-requested consolidated validation starts. Fix any harness linkage/compilation errors and assert the injection point corresponds to intended model/proxy buffer creation. The RecordingDevice mock does not track buffer cleanup or GPU submission lifetimes.
2. Test HLOD proxy upload failure specifically via allocation labeling or tagged test-device instrumentation; the current first-allocation assumption is not a guaranteed HLOD identification contract.
3. Cover GPU idle refusal and staged-retirement ownership: Stage 26's local `GeometryScene staged` may orphan pending models if GPU idle fails. Add host-owned retryable retirement storage rather than releasing references without proof.
4. Finish staging remaining `RtsView` GPU owners (terrain/fog/overlays/roads/shores) and reconcile latest MotionWeave in a real conflict-aware merge.
5. Then run consolidated native configure/build/CTest plus Metal/Vulkan/GLES backend validation before considering main merge.
