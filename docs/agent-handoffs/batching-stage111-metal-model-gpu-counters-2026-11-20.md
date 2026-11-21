# AIVERSE — Stage 111: GPU counter samples around shadow and opaque model draws

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/auto-batch-cost-metrics-20261119`  
**Pull request:** #39 (Draft)

## Motivation and previous evidence

Stage110 owner Mac Studio validation confirms full-command-buffer GPU regression of CPU MDI over Direct, despite CPU render() savings. In matched D/M/M/D runs, 2K primitive model workload GPU command-buffer medians were Direct 0.59449ms / CPU MDI 0.81618ms (+37.29%); 4K Direct 0.93573ms / MDI 1.44369ms (+54.28%). This metric is **whole GPU command-buffer time** and cannot distinguish shadow, opaque and unrelated GPU work. All eight Stage109 runs passed 38 CPU checks, raster parity, and 66 valid GPU command-buffer timestamps each. Neither real FPS nor model-pass GPU work was previously measured.

Apple's Metal GPU counter sample API supports optional draw-boundary sampling via `MTLCounterSampleBuffer`, with counter sets and supported sampling points exposed by the device. It requires pass descriptor counter sample buffer attachments and device support. Samples must be resolved only when GPU work has completed. The GPU timestamp values can be in hardware clock units, so an uncalibrated delta must **not** be printed as milliseconds. Reference: https://developer.apple.com/documentation/metal/sampling-gpu-data-into-counter-sample-buffers and https://developer.apple.com/documentation/metal/gpu-counters-and-counter-sample-buffers .

## Code, API, modified files

1. `engine/gfx/Gfx.h`, commit `b121f9f22dcb7a652b78bab1a31c26fb8b7614ae` — additive virtual interface: `enableModelGpuCounters(bool)`, `sampleModelGpuCounter(int phase, bool start)`, `takeModelGpuCounters()`, `ModelGpuCounter{frame, shadowTicks, opaqueTicks, valid}`. Default backend implementation explicitly rejects counter support and returns empty observations (never fake zero).
2. `engine/gfx/metal/GfxMetal.mm`, commits `17fb7fd9379940a076088fabe3862e65fb4efa2e`, `66ca87d8991a36cc83c5351c4ac58ff980d9637c` — use `device.counterSets` to locate the `MTLCommonCounterSetTimestamp` set and the timestamp counter, check draw-boundary sampling support, create a 4-sample shared `MTLCounterSampleBuffer` on each enabled frame. Attach to Metal render pass descriptor and call `sampleCountersInBuffer:atSampleIndex:withBarrier:NO` from an active render encoder. Four marks correspond to start/end of shadow and opaque. In the command-buffer completion callback, resolve four `MTLCounterResultTimestamp` records, reject zero/error/decreasing timestamps, and append tick differences to a mutex-protected queue owned by a `shared_ptr`, avoiding object lifetime races. No blocking wait is added. No buffer residency, frame fence, ICB command encoding or shader state changes.
3. `engine/render/Renderer3DModels.cpp`, commit `0e99d6575049f588344a2810e129b5e8407fb9dd` — non-intrusive GPU sample markers around eligible non-auxiliary `renderModels(0)` and `renderModels(1)` spans. Other phases/auxiliary views excluded. These markers surround GPU commands emitted within the model-rendering routines, not the entire shadow or opaque render pass, and their timestamps are not guaranteed to exactly equal GPU busy time.
4. `tests/render/material_batches.cpp`, commit `8362a38721a92c2368a687643284f1f235079b5e` — opt-in `AIVERSE_BATCH_MODEL_GPU_COUNTERS=1` alongside existing `AIVERSE_BATCH_GPU_TIMELINE=1`; capability check at fixture init, no fake success on unsupported hardware, collect completion results each frame, emit `<prefix>-model-gpu-counters.csv` for 66 logical frames 115..180 with columns `frame,shadow_gpu_ticks,opaque_gpu_ticks,valid`. Fail fixture if not exactly 66 valid readings.
5. `tools/batching-stage111-analyze.py`, commit `5e61931b80d7abe76884e5123d989894c22a9f94` — parse 66 model-counter samples plus 66 GPU CB timings and 81 CPU render frames per mode, check real Direct/CPU MDI paths, report median and P95 for shadow/opaque **raw ticks**, full-CB GPU milliseconds and CPU renderer milliseconds, output `stage111-summary.csv`.
6. `tools/batching-stage111-trials.sh`, commit `43c2126506b67e53d5d5daac6d01bc7384d90454` — four isolated native processes 2K Direct/MDI and 4K Direct/MDI; mandatory CPU invariants, pixel parity and 66 valid GPU counters, unique archive and manifest.
7. This handoff document is created in its own subsequent commit.

### Architectural notes / caveats

- GPU tick differences are not calibrated nanoseconds or milliseconds. Per-run ratios on one device may diagnose stage changes but could also be affected by GPU frequency and draw-boundary sampling semantics; do not convert using fabricated clock rates.
- Metal sampleCounters support is hardware/OS dependent; code explicitly validates `MTLCounterSamplingPointAtDrawBoundary` and the timestamp counter set, and the test *fails* when unavailable. It does not attempt to modify OpenGL ES or Vulkan render paths.
- `withBarrier:NO` deliberately avoids GPU stalls introduced by diagnostic barriers. Counter timestamps may be less strictly ordered than a barrier mode; if invalid, do not claim any GPU results, capture native evidence first.
- Instrumentation is opt-in. Even when enabled, shadow/opaque counters add sample commands and shared sample buffer allocations and may alter runtime. Compare both modes with counter sampling enabled. The existing Stage108 GPU CB timestamps remain the independent whole-frame control.
- A renderer model pass may begin or end without an active encoder on unusual scenes; sample results then remain invalid rather than silently report zero. The targeted synthetic stress fixture has known shadow + opaque model geometry and one relevant renderModels call each.
- No model-level shader instructions, overdraw, GPU occupancy, GPU counters beyond timestamps, actual FPS, power or real-game workload have been measured.

## Actual verification status

**Performed:** source inspection via GitHub connector of existing Metal `timeGpuFrame`, RHI callbacks, Renderer3D shadow and opaque rendering boundaries; review of Apple Metal documentation; committed source and scripts. **Not performed:** compilation of new Objective-C++ code on owner Mac, script syntax or Python analyzer tests on native output, 66 model GPU samples on hardware, Vulkan/GLES compilation or parity, real-game scene and full CTest. All runtime performance numbers cited belong to **previous Stage109**, not the untested Stage111 code.

## Exact Mac Studio native validation (new worktree)

Previous worktrees can have in-progress rebases; do not reset or switch them. Create a detached worktree:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage111
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage111" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage111" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage111-trials.sh || exit 1
python3 -m py_compile tools/batching-stage111-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage111/aiverse_material_batches \
  bash tools/batching-stage111-trials.sh
```

On a native error, capture the *first* AppleClang/Metal compiler diagnostic and update code before rerun. If no hardware counter support, report `GPU model-pass counters UNSUPPORTED` and treat as a capability limitation, not a zero-time pass. On successful execution share the printed `Stage 111 archive:` path as uploaded archive; analyze four GPU model-ticks CSVs, GPU CB times and Direct/MDI parity. Before merging or altering Metal performance paths, independently run representative game-scene GPU profiling and confirm that any scoped GPU regression actually comes from ICB GPU execution rather than camera, shadow or driver scheduling.

## Next agent step

1. Native Mac compile, correct any Metal SDK availability or sample-buffer attachment problems, record exact actual tests.
2. Confirm 66 valid samples under explicit opt-in, using more detailed GPU capture if samples are unsupported/invalid.
3. Compare shadow vs opaque counter ticks across Direct and MDI, while preserving Stage109 whole-CB measurements.
4. Decide where to optimize Metal ICB only after measured evidence; keep PR Draft and all protections/opt-ins in place.
