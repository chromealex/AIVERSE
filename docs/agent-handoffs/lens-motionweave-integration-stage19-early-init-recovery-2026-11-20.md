# AIVERSE LENS Stage 19 — retryable GPU-idle recovery of early sprite initialization (2026-11-20)

## Branch and actual commits
- Branch: `feature/lens-motionweave-integration`. `main` untouched.
- `3c9859236f782176afeb0932f0f3bb398f75d6fe`: `engine/render/SpriteBatch.h` adds retryable `shutdownAfterGpuIdle()`.
- `94b8f78ece37979cc3b75c13de331fb8fb461ff3`: `engine/render/SpriteBatch.cpp` implements idle-gated shutdown and resets destroyed GPU handles to allow subsequent initialization.
- `ed530bd33f08b9468cc085f81cc112593cd43b83`: `modules/rts/view/Rts2DView.h` adds `shutdownAfterGpuIdle(gfx::Device&)`.
- `c7a8fd1c4f59b8567078311caf30208f48c930d4`: `modules/rts/view/RtsSprites.cpp` moves ALL init stages, including first/second batch initialization, under a common try/catch transaction; on failure it requests GPU idle before destroying any allocated resources.
- `5bb01e7fa4e4b2e6656cd8d34474a98bb43c9426`: corrects the sprite pipeline error log newline escape.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage18-exception-gpu-rollback-2026-11-20.md`.

## API, lifecycle and design
- `SpriteBatch::shutdownAfterGpuIdle()` returns false without modifying handles if the GPU idle barrier fails. On success calls `shutdown()`. `shutdown()` now checks buffer handle validity and clears buffer/pipeline/shader handles after destroying them; this is still an unsafe low-level cleanup function if the caller has NOT established idle.
- `Rts2DView::shutdownAfterGpuIdle(dev)` waits for the device idle and invokes its existing `shutdown(dev)`. On barrier failure it returns false, preserving texture cache and both sprite batches for an explicit later retry.
- `Rts2DView::init` rejects reuse while `ready_` or `rules_` indicates active/recovery-pending ownership. Setup of `rules_`, base directory, texture cache, primary batch, secondary batch and generated/loaded sprite assets is guarded by the same exception cleanup.
- If the first or second batch returns false, `init` attempts guarded teardown and returns false. If a batch throws (including its own idle-barrier failure) or a texture loader throws, the outer catch attempts GPU-idle cleanup. If that fails too, the view remains in a recovery-pending state.
- No game-genre dependencies added to generic engine code. ECS, save schema, scene layout and camera APIs remain unchanged; new recovery entry points are additive.

## Actual checks
- Read Stage 18 handoff, source of both classes and texture cache lifecycle; compared edits through GitHub.
- Re-fetched all four modified source files after commits; checked presence of rollback hooks, resource reset, early batch try/catch and Stage 18 camera RAII.
- CMake configure, C++/Objective-C++ compiler, CTest, GPU fault injection, Metal/Vulkan/GLES rendering: **NOT RUN**. User prefers a single consolidated test phase.
- No claim of compilation success or runtime GPU safety.

## Residual caveats and next actions
1. `Rts2DView::shutdown(dev)` and `SpriteBatch::shutdown()` are still low-level operations. Call only when GPU idle is already known or on final device teardown. Audit existing host call sites to ensure that contract; prefer the new guarded entry points for normal teardown.
2. Failure to establish GPU idle is deliberately reported and ownership retained, but no automated deferred-retirement queue or explicit device-loss handling is added yet. The host must retry when the device is responsive or dispose the entire device at final teardown.
3. C++ compiler must check the local Stage 18 `RestoreCamera` private member access and the updated multi-type handle reset assignments. Do not merge without native validation.
4. Add fault-injection tests for shader/pipeline/vertex/index buffer allocation failures, partial GPU idle failures, early texture exceptions, repeated recovery, and successful re-init.
5. Confirm Metal/Vulkan/GLES per-pass buffer snapshot and >16384 indexed quad behavior in the consolidated native run; inspect cleanup code on each backend.
6. Integrate latest MotionWeave with inspected three-way conflict resolution, then conduct user-requested full build/CTest and GPU backend verification.
