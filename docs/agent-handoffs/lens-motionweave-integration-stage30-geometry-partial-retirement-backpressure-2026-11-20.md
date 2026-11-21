# AIVERSE LENS Stage 30 — bounded GeometryScene failures, partial GPU retirement and explicit idle recovery (2026-11-20)

## Branch and exact commits
- Repository: `chromealex/AIVERSE`; integration branch `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation `3f1755a16178cfb2d95663e4a8aaee088aae3912`: `engine/render/GeometryScene.h` — `kMaxFailedStages=8`, rejection counter, explicit `recoverFailedStagesAfterGpuIdle(gfx::Device&)`.
- Implementation `df7507ee304dd08de34b09705e9e61c112c52280`: `engine/render/GeometryScene.cpp` — refuse staging at the cap before allocating/reading/uploading new models, retain individual staging owners until their collection actually succeeds, retry a known-idle recovery and reuse it at shutdown.
- Tests `5941b2c7e1463a9e4b526755f96258d6a83478b1`: `tests/geometry/runtime.inc` — per-token partial completion and backpressure/idle-recovery tests.
- Test harness fix `9dca2fefe079e0bd8597ff20b38f02ae6f5f7230`: `tests/tests.cpp` — synchronous `RecordingDevice` reports successful `waitForResourceIdle` unless an explicit fault-injection subclass overrides it. Prevents normal fake-device cleanup from being misclassified as an unresponsive GPU.
- Prior handoff `docs/agent-handoffs/lens-motionweave-integration-stage29-retained-geometry-retirement-2026-11-20.md`.

## Architecture / API
- A live `GeometryScene` holds at most eight failed staging scenes. When all eight are retained, `loadTransactional` increments `rejectedFailedStageLoads_` and throws a descriptive backpressure error **before** performing any new staging allocations, file reads or GPU uploads. The currently published scene remains intact. The cap is on the number of retained stages, not an estimated byte budget.
- `collectRetired` still uses GPU completion tokens and must only be called at the render-owner frame boundary. One completed token only frees its own staging owner; other incomplete/failed tokens remain owned. The owner is erased only if the collection call succeeds AND its pending resource count is zero.
- `recoverFailedStagesAfterGpuIdle(dev)` is an explicit host-triggered blocking retry, never called during a draw pass. It walks retained stages and asks each `NativePresentationRetirement` queue to establish idle and drain. If one fails, it returns false while keeping that stage and subsequent stages; previously proven-complete stages may already have been erased.
- `shutdown` uses the same retryable recovery method for failed stages and afterwards drains the live retirement queue. No hidden background work or device-loss deletion is introduced.
- Additive public API: `kMaxFailedStages`, `rejectedFailedStageLoads()`, `recoverFailedStagesAfterGpuIdle`. No ECS schema, asset/JSON format or save compatibility changes, and no genre dependency was added to `engine/render`.

## New regression tests (AUTHORED, NOT EXECUTED)
1. `geometry_failed_stages_collect_only_their_completed_gpu_tokens`: a mock device returns two distinct GPU retirement tokens. Neither stage can be released before its token completes; after the first completes, exactly one owner is removed; after the second, both are gone. The previously published scene stats must be unchanged.
2. `geometry_failed_stage_backpressure_and_explicit_idle_recovery`: eight consecutive staged GPU allocation failures with idle refusal retain eight owners; a ninth attempt fails before model reads and buffer allocation; unsuccessful recovery retains all owners; successful recovery after restoring idle empties the failed-stage list, allowing an ordinary replacement to succeed.
3. `RecordingDevice` is CPU-only and synchronous; its true idle result is **not** proof about Metal/Vulkan/GLES runtime. Derived `GpuUploadFailDevice` and `GpuTokenDevice` explicitly simulate refusal and individual completion.
- Tests use the existing `tests/geometry/runtime.inc` harness included by `tests/tests.cpp`. No new CMake target in this stage.

## Checks actually performed
- GitHub source inspection of `AsyncResourceRetirementQueue.h`, `NativePresentationRetirement.h`, `GeometryScene.h/.cpp`, existing geometry tests and Stage 29 handoff.
- Four source commits succeeded, and all edited files were fetched back to verify cap checks, retry method, success-gated owner removal and test cases are present.
- **NOT RUN:** CMake configure, C++ compilation, CTest (including new and existing geometry/fault tests), native Metal/Vulkan/GLES build and device-backed GPU validation, pixel parity and performance profiling. Do **not** claim tested or green.

## Known risks, limitations and concrete next step
1. The eight-stage cap prevents unbounded failure count, **not** large-model memory growth within each stage. Assess memory-based budgets/estimated pending bytes and explicit device-loss policy in a later stage if needed.
2. Real GPU tokens can fail, be delayed, or be issued on a different device-generation. The current borrowed-device ownership and mock tokens do not establish full device-loss safety; do not destroy GPU-live resources without completion proof.
3. Native compile might reveal pre-existing Stage 18–29 integration errors. First compile the consolidated LENS branch and run the CTest geometry targets including the new cases; fix failures and record exact results.
4. Stage 26's transactional replacement does not make the entire `RtsView` reload atomic; terrain/fog/overlays/roads/shores and other resource owners remain to be staged. Preserve public source APIs and prevent repeated simulation/animation.
5. Reconcile `feature/universal-ecs-animation-pipeline` against current integration HEAD with a true merge-base and explicit per-file conflict resolution, especially `apps/modelview/main.cpp`. Then perform the requested consolidated build, complete CTest and backend-specific Metal/Vulkan/GLES validation before merging `main`.
