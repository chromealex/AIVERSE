# AIVERSE — Stage 108: asynchronous Metal GPU command-buffer timing

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)

## Goal and baseline

Stage 107 established correct residual Direct fallback and 1K/2K/4K CPU wall speedups for CPU MDI. The 2K/4K matched process runs yielded 32.12%/36.14% lower CPU `renderer.render()` medians with MDI, with pixel parity. GPU execution timing was absent. The engine RHI already contains `gfx::Device::timeGpuFrame(uint64_t)` and `takeGpuTimings()`, implemented in Metal using asynchronous command-buffer `GPUStartTime`/`GPUEndTime` after completion. This stage **reuses** those existing APIs; it does not add another Metal timer or synchronize the GPU.

## Changed files and contracts

1. `tests/render/material_batches.cpp` — new **opt-in** `AIVERSE_BATCH_GPU_TIMELINE=1` for `auto_stress`. Call `timeGpuFrame(frame)` for frames 115..180 and poll `takeGpuTimings()` at the start of subsequent fixture frames. On shutdown poll one final time and write `<prefix>-gpu-frames.csv` with fields `frame,gpu_command_buffer_ms,valid`, requiring 66 unique valid consecutive GPU completion samples. The normal fixture continues to write its existing CPU-only frame timeline. No sync wait inserted. This GPU metric is the **whole Metal command-buffer GPU timeline** and may include rendering, other work, idle gaps and presentation-related commands. It is NOT model-only GPU time, actual GPU occupancy/utilization, FPS or a guaranteed sub-pass duration.
2. `tools/batching-stage108-analyze.py` — validates 66 GPU tags (115..180) and 81 CPU frames (115..195) for 2K/4K Direct/MDI; checks actual Direct/MDI mode and 0 vs substantial indirect command counts; outputs median, P95, max whole-command-buffer GPU timestamp time plus CPU render() median/P95; writes `stage108-summary.csv`.
3. `tools/batching-stage108-trials.sh` — four native process runs (2K Direct, 2K MDI, 4K Direct, 4K MDI) with CPU and GPU timing flags, real MDI raster parity when enabled, 38 CPU checks, final raster parity, actual command submission counts and 66 valid GPU timestamp samples. Emits unique timestamp+PID capture directory, logs, CSVs, provenance manifest and a single archive for user transfer.
4. This handoff document.

**Commits:** `4631bb3ead22d28ccbb7c8f9d71f085da8bd21be` (fixture), `f8da7d5b31d3067316a2895dbbb8f0570033cdc2` (explicit `<cmath>`), `a372c198855beb7748c24bb73bc06005a7000c9b` (analyzer), `60c488c796963261b0b050b9be3f637f60b85ae9` (runner). This handoff is a subsequent commit.

## Tests actually performed vs pending

**Performed:** GitHub repository code inspection of the existing Metal `timeGpuFrame` API and its completion-time handling, branch source changes, source-level review of frame bounds and benchmark flags. **Not performed:** macOS native compile of the changed fixture, shell/analyzer execution, timing samples, GPU performance comparison, cross-platform Vulkan/GLES timing tests, high-load real scene or full CTest. It is unknown whether this Mac Studio's Metal command buffer timestamps are consistently available in the fixture. If GPUStartTime or GPUEndTime is unsupported/zero, Stage108 **must fail explicitly**; do not fill missing samples with fake zeros or claim GPU time.

## Mac Studio validation — isolated detached worktree

Do not reset/switch previous rebasing worktrees. Create a new detached worktree with the branch, compile targeted native fixture and launch:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage108
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage108" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage108" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage108-trials.sh || exit 1
python3 -m py_compile tools/batching-stage108-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage108/aiverse_material_batches \
  bash tools/batching-stage108-trials.sh
```

Send generated `build/batching-stage108/captures/capture-....tar.gz`; it includes all four GPU CSVs, CPU CSVs, logs and report. If any assertion fails, send failing `*.log` and do not claim Stage108 completed. Do not run screenshots through the GPU timing path as a production benchmark without checking that extra capture and frame-end operations are matched between Direct and MDI.

## Next engineering priorities

1. Confirm whether CPU MDI lowers CPU time while preserving acceptable **whole GPU command-buffer** timeline time, without conflating it with model-only GPU execution.
2. Run matched trials more than once, varying order across runs to control device/driver thermal drift; the four process tests alone cannot establish stable GPU improvement.
3. Profile real representative mixed-model scene and actual FPS/frame pacing (not just unique triangle primitive stress).
4. Validate Vulkan and GLES fallback and parity on corresponding devices/build configurations before enabling an opt-in implementation by default.
5. Maintain PR #39 Draft and per-stage handoffs; do not alter renderer safety bindings or AUTO thresholds without measured evidence.
