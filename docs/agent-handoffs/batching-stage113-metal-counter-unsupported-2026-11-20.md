# AIVERSE — Stage 113: Metal draw-boundary timestamp sampling unsupported on owner Mac

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Code commits:** `790423652279f974a884f29b27c3cdb3d115afec` (fixture) and `4002ef465f2a713e93bfb921cc76fd99985ae984` (runner). This handoff is a separate commit.

## Actual Mac Studio result

Owner compiled the Stage111 Metal GPU counter-sampling implementation successfully with AppleClang 17 and attempted the `2048 direct` run. Native log from `build/batching-stage111/captures/capture-20261120T075957Z-30612/2048-direct.log` contains:

```text
GPU model-pass counters UNSUPPORTED by this device
GPU model counter timeline INVALID: valid=0 expected=66
GPU command-buffer timeline INVALID: valid=0 expected=66 unsupported=0
material palette CPU checks=38 result=passed
stress requested_items=2048 authored_unique_primitives=2048 submitted_model_draws=1
```

The first line is the **root cause of the skipped experiment**. The Stage111 probe requires Metal `MTLCounterSamplingPointAtDrawBoundary` and an available timestamp counter set; the owner device/SDK combination does not offer that capability through the current implementation. No draw-boundary GPU samples were collected and no GPU shadow/opaque results can be reported. The later two "INVALID 0/66" lines are **secondary** fixture shutdown effects after initialization stopped early, not proof that full-command-buffer GPU timestamps have stopped working; earlier Stage108/109 completed 66 such timestamp samples per trial. The full four-process Stage111 experiment was not executed. This is a capability limitation of this diagnostic API, not evidence of a Metal CPU-MDI correctness failure.

## Code updates

1. `tests/render/material_batches.cpp`: add `modelCountersUnsupported` latch during fixture init; on missing capability print specific `GPU model-pass draw-boundary timestamp counters UNSUPPORTED` and prevent shutdown routines from attempting to export 66 GPU model and whole-buffer samples from a run that never started. Return exit **77** from the fixture to distinguish explicit unavailable sampling from genuine test failures (exit 1); no fake 0ms values. All existing runtime rendering and GPU submission paths remain unchanged, and the GPU sample functionality remains explicitly opt-in.
2. `tools/batching-stage111-trials.sh`: explicitly catch exit 77 and the expected unsupported log marker, record `result=UNSUPPORTED`, `capability=MTLCounterSamplingPointAtDrawBoundary` in manifest and save a `capture-....tar.gz` containing the actual failed capability probe log. Exit 0 with clear "UNSUPPORTED" description, not a false assertion of benchmark PASS. Any other exit status still invokes the existing ERR trap and prints the current native log and saves a FAILED archive. No GPU/CPU performance numbers are emitted for unavailable counters.
3. This handoff contains source-level status and continuation steps.

**Default safety:** no changes to renderModels' default behavior, Metal ICB encoding, shader uniforms, GPU resource residency, frame fences, runtime options, model batching AUTO policy, Vulkan/GLES or backend logic. Code is diagnostic only.

## Validation status

**Actually performed:** inspected exact user-supplied native log and current RHI/fixture/runner code through GitHub; implemented explicit unsupported path and error classification; committed source updates. **Not performed:** native Mac rebuild or execution of Stage113 code, GPU scoped timing, Metal Instruments frame capture, new GPU performance evidence, CI, Vulkan/GLES compile. Do not assert 113 native PASS until owner compiles and runs it.

## Recommended next action

**Do not repeat a four-mode Stage111 benchmark on this hardware simply to obtain an unsupported result.** Stage108 and Stage109 already demonstrate a reproducible whole-command-buffer GPU timeline regression for CPU MDI: ~+37% GPU at 2K and ~+54% at 4K in Stage109 while CPU render() is lower. CPU MDI remains opt-in / not default enabled; PR #39 remains Draft.

Use **Xcode Metal GPU Frame Capture** or Instruments' GPU tools on the existing real Direct/MDI stress fixture with the unsupported `AIVERSE_BATCH_MODEL_GPU_COUNTERS` flag **disabled**. Capture comparable warm frames at 2K or 4K, inspect per-encoder GPU execution and per-draw resources. This avoids attempting unsupported `MTLCounterSamplingPointAtDrawBoundary`. GPU Frame Capture availability/counters depend on macOS/GPU/Xcode; report whatever detailed metrics the device supports rather than invent timings.

A minimal *code validation only* (no repeated GPU campaign) on owner's existing Stage111 detached worktree is possible after fetching branch updates, rebuilding targeted fixture in a separate clean Stage113 worktree (prefer separate worktree to preserve any other agent state), and running the fixture with counters enabled once. Expect exit 77 and explicit UNSUPPORTED; no model GPU data is expected. This is optional because underlying unavailable capability has already been observed.

**Next agent:** prioritize Metal GPU frame capture from representative Direct/MDI scene and, if instrumentation is possible, isolate shadow vs opaque GPU work via offline capture without changing render pass semantics. Compare all findings to established CPU and whole-command-buffer control data. Never disable explicit ICB buffer bindings, residency or synchronization solely because of the counter sampling failure. Continue stage-by-stage handoff docs.
