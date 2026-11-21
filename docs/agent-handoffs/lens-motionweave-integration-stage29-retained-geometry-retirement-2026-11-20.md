# AIVERSE LENS Stage 29 — retained failed GeometryScene staging and retryable GPU retirement (2026-11-20)

## Commits and modified paths
- Branch: `feature/lens-motionweave-integration`; `main` untouched.
- `c3290eb851721f1a35a9888dbc4f2ba05c07c3da` — `engine/render/GeometryScene.h`: explicit `failedStages_` ownership, `pendingFailedStages()` diagnostics, aggregate pending GPU retirement and frame-boundary `collectRetired`.
- `b7adf35d2091152dec340c61dfec5589833a3b01` — `engine/render/GeometryScene.cpp`: transfer failed staging scene into the live owner when `completeForShutdown` fails; retry collection and shutdown.
- `191130ead472b5edaca83a9d0ea0b55c3c90579b` — `tests/geometry/runtime.inc`: add `geometry_failed_gpu_idle_preserves_staging_retirement_for_retry`.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage28-geometry-gpu-faults-2026-11-20.md`.

## Architecture
1. `GeometryScene::loadTransactional` pre-reserves a slot in `failedStages_` *before* attempting new GPU uploads and constructs its staging scene as a `unique_ptr`. On an upload exception, `loadImpl` has already enqueued partial uploaded Models/HLOD into that staging scene's own `NativePresentationRetirement`.
2. If the staging retirement queue cannot prove idle via `completeForShutdown`, the original live scene retains the complete failed staging scene in `failedStages_` and rethrows the original error. Previous visible model instances/stats are unchanged.
3. `collectRetired(dev)`, called at a render-owner frame boundary, now collects both the existing live retirement queue and all retained failed-stage queues. A staging owner is erased only when its pending retirement count reaches zero.
4. `shutdown(dev)` also attempts `completeForShutdown` on each failed stage. On barrier failure it throws and retains the failed-stage owner for a later retry. Once all failed stages complete, the live queue is drained as before.
5. `pendingGpuRetirements()` now aggregates live and failed-stage retirement queues; `pendingFailedStages()` provides a distinct count for fault diagnostics. No ECS schema, serialization or genre dependencies added to the renderer.

## Test coverage authored (NOT EXECUTED)
- The new test first loads a visible scene, injects a GPU buffer allocation failure after one new model's expected uploads and refuses idle. It asserts that the visible scene survives, an owned failed stage exists and its retirement count is nonzero. It then enables idle, performs a successful replacement and calls `shutdown` to verify that all stages are retired.
- Uses existing `GpuUploadFailDevice` mock. This models allocation failures and idle refusal, not actual queued GPU commands; source ordering and cleanup still need compiler and runtime verification.

## Validation actually performed
- Examined the current `GeometryScene.h/.cpp`, `NativePresentationRetirement.h`, the existing geometry tests and Stage 28 handoff in GitHub.
- Updated three files and re-fetched to verify the recovery retention, collection and shutdown logic and test function are present.
- **NOT RUN**: native CMake configure/build, CTest, Metal/Vulkan/GLES backend builds, device-backed tests, screenshot/pixel parity, stress/performance measurements.

## Risks, limits and next actionable steps
1. A permanently lost device still cannot be safely drained; retained failed stages must remain owned until host device teardown or explicit generation-aware recovery. This change prevents *premature owner destruction*, not permanent device-loss cleanup.
2. `failedStages_` may grow across successive failed reloads while the GPU is unresponsive. Add bounded diagnostics/backpressure and verify allocator failure recovery for the pre-reserve path.
3. Add validation for failed-stage queue partial collection and retry. Confirm no CPU Model destruction while any GPU token is pending, and no stale StaticModelCache pointers after scene swap.
4. Compile and run existing Stage 27/28/29 geometry tests; examine `GpuUploadFailDevice` assumptions and the Stage 18 camera RAII compile path. Then reconcile MotionWeave in a real three-way merge and complete the requested consolidated CTest + Metal/Vulkan/GLES validation before `main`.
