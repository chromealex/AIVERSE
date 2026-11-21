# AIVERSE Stage 114 — programmatic Xcode Metal GPU Frame Capture

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Latest code commits:** `1751d6de6b843c7be572a7ebab1a6d073452afad` (`GfxMetal.mm` frame capture), `3ee51698e956529077f72e7e1b7554b552901a84` (`Gfx.h` debug groups), `e17d5b3b7272ac38a7e2e163c8c2634dbae695a2` (Metal debug groups), `6b5af351f616c85217c647dfbd4a20c49bd6d6d2` (`Renderer3DModels.cpp` scoped labels), `91a5582a41ee59402aafa8a818271a0b2785b51c` (capture runner). This handoff is a separate later commit.

## Goal and previous evidence

Stage 109 confirmed CPU MDI improves **CPU render()** medians (~35.6% at 2K, ~32.6% at 4K), but worsens **whole Metal GPU command-buffer** median durations (~+37.3% at 2K and +54.3% at 4K) compared with Direct on the same static unique-primitive scene, despite pixel-identical raster output.

Stage 111 attempted GPU per-draw-boundary counter sampling, but the owner's Mac Studio reported `GPU model-pass counters UNSUPPORTED by this device`. Stage113 correctly treats that unsupported API as a separate capability status, not missing GPU work. This stage uses a **different, Apple-documented Metal GPU Frame Capture mechanism** so the user can inspect actual GPU command streams in Xcode without requiring draw-boundary hardware timestamp counters.

Apple's official guide: https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically . On macOS 14 or later, opt-in GPU capture can be enabled at process launch with `MTL_CAPTURE_ENABLED=1`. `MTLCaptureManager` supports capturing to a `.gputrace` file with `MTLCaptureDestinationGPUTraceDocument` when `supportsDestination` returns true. Capture starts **before** the target command buffer is created and stops after it has been committed. It may add CPU/GPU overhead: do **not** treat a capture run as a throughput benchmark.

## Implementation and contract

### Metal backend: `engine/gfx/metal/GfxMetal.mm`

**Opt-in environment variables:**
- `AIVERSE_METAL_TRACE_FRAME=131` — target backend `frameSerial()`, not the fixture's zero-based frame index. It corresponds approximately to logical fixture frame 130 with the current platform loop.
- `AIVERSE_METAL_TRACE_PATH=/absolute/path/to/2048-direct.gputrace` — destination; must end in `.gputrace`.
- `MTL_CAPTURE_ENABLED=1` — mandatory on macOS 14+ for programmatic captures when the app's Info.plist does not enable MetalCaptureEnabled.

At `beginFrame`, after incrementing the backend serial **before** constructing `cmd_`, the backend attempts an Apple `MTLCaptureManager` GPUTraceDocument capture once, scoped to its Metal command queue. It validates frame/path, `supportsDestination`, and `startCaptureWithDescriptor:error:`; it logs `[metal-gputrace] START...`, `UNSUPPORTED` or `FAILED`, never silently claims success. At `endFrame`, after `[cmd_ commit]`, it calls `stopCapture` and logs `[metal-gputrace] STOP...`. The runner checks that the requested `.gputrace` exists (a GPU trace may be a directory/package). **No trace is started unless both opt-in environment variables are present.** iOS capture path is compiled out in this stage; behavior for the iOS Metal runtime remains unchanged.

### Render debugging labels

`engine/gfx/Gfx.h` exposes optional `pushGpuDebugGroup(const char*)` and `popGpuDebugGroup()` with safe no-op defaults for Vulkan/GLES/other backends. Metal implements these using `MTLRenderCommandEncoder` debug groups only while the opt-in GPU frame trace is active.

`Renderer3D::renderModels` opens labels `AIVERSE Shadow Models` and `AIVERSE Opaque Models` inside their existing GPU render encoders. A local scoped RAII pop safely balances early returns, including empty order lists. No shader, culling, material, state, MDI command, residency or render pass changes.

### Runner: `tools/batching-stage114-capture.sh`

Run with argument `2048` (default), `4096`, or `both`. Creates a unique UTC/PID capture folder under `build/batching-stage114/traces`, launches a Direct and a forced-CPU-MDI native fixture process for each requested count. Uses `MTL_CAPTURE_ENABLED=1`, `AIVERSE_METAL_TRACE_FRAME=131`, and a per-process unique absolute `.gputrace` destination. **Explicitly disables** unsupported `AIVERSE_BATCH_MODEL_GPU_COUNTERS` flag, prior AUTO/GPU ICB experiments and per-submission trace flags. Requires `[metal-gputrace] START/STOP` in each log, actual trace path exists, native 38 CPU checks PASS, final raster parity and actual MDI frame-100 pixel parity when applicable, and real 81-frame MDI vs Direct. The output is Xcode-replayable GPU trace files, not an automated numerical GPU model-pass comparison. The runner deliberately does not tar potentially very large gputrace packages; share paths or Xcode captured data/screenshots instead.

## Validation truth

**Done:** verified Apple official programmatic GPU capture documentation and inspected repo Metal beginFrame/endFrame and model rendering code; GitHub commits for backend capture path, capture debug groups, Direct-vs-MDI test runner. **NOT DONE:** native AppleClang build, actual Mac Studio `.gputrace` creation, trace opening in Xcode, GPU model pass timing attribution, Vulkan/GLES build or platform tests, any GPU optimization. Previous Stage109/111 results apply only to the previously tested versions. Keep PR Draft and CPU MDI off by default. The API may be unsupported without proper process-level Metal capture configuration or tools; on failure read the exact `[metal-gputrace]` message and do not fake captures or durations.

## Safe Mac Studio steps

Use a **new detached** worktree to avoid disturbing other branches/rebases:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage114
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage114" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage114" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage114-capture.sh || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage114/aiverse_material_batches \
  bash tools/batching-stage114-capture.sh 2048
```

Check the printed path `build/batching-stage114/traces/capture-...-PID/` for `2048-direct.gputrace`, `2048-mdi.gputrace`, the two `.log` files and manifest. Open both GPU traces in Xcode using Finder or `open -a Xcode /absolute/path/file.gputrace`. In the GPU debugger, inspect render pass and debug groups, compare per-draw/ICB execution, GPU shader workloads and buffer resource binding as the device supports. **Capture overhead invalidates FPS/performance comparisons**; compare semantics and expensive work distribution instead.

If 2048 succeeds, optionally run `bash tools/batching-stage114-capture.sh 4096` for the 4K case. If capture output is absent, share the native `2048-direct.log` and check Metal capture environment support. Do not assume the GPU trace can be parsed as plain text without Xcode.

## Following work

From actual Xcode capture evidence, identify whether ICB execution overhead occurs in shadow, opaque or both. Avoid removing Metal ICB buffer residency declarations and safety workaround for AGX driver faults. Optimize only after locating and verifying model-specific GPU cost. Retain default-off feature flags, Draft PR #39 and a per-stage handoff after each change.
