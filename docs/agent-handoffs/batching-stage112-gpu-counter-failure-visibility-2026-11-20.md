# AIVERSE batching — Stage 112: report native Metal counter failure instead of silently exiting

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Source commit:** `bd327a893e253a86678e33dfb0964c5b482b8cff` modifies only `tools/batching-stage111-trials.sh`.
**Handoff:** this file, in a separate commit.

## Exact owner native result

Owner uploaded the Mac Studio terminal log `Pasted markdown(20261120-080200).md`. It establishes:
- Detached worktree at `c851ea382`, CMake configure success with AppleClang 17.
- Complete native build of the Stage111 Metal counter-sampling implementation: `[100%] Built target aiverse_material_batches` with `GfxMetal.mm.o`, `Renderer3DModels.cpp.o` and fixture compiled.
- The runner printed `Stage 111: 2048 unique primitives; direct` and returned to shell immediately, with **no** `PASS` line, no next MDI/4K trial, and no CPU/GPU counter summary.
- The runner redirected the fixture's stdout/stderr to the private `2048-direct.log`, and `set -e` exited without emitting that log. Thus the *actual reason remains UNKNOWN*. Unsupported device counter-sampling, invalid sample results, Metal command-buffer runtime errors or a fixture assertion are possibilities, not established findings. Do not claim Stage111 GPU model-pass counters are valid or supported.

## Code fix

The runner now sets a per-run `CURRENT_LOG`, installs a Bash `ERR` trap, and on any failed subprocess/assertion/analyzer:
1. Shows the actual exit status and unique run capture directory.
2. Prints the last 70 lines of the failing `2048-direct.log` (or the current mode).
3. Records `result=FAILED` in `manifest.txt` and creates `capture-....tar.gz` even for failure, preserving diagnostics for transfer.
4. Exits with the original nonzero status; never reports a successful GPU measurement by accident.

**No RHI/Metal/renderer code changed** in Stage112. Default-off GPU counters remain default-off. Fix solely improves native experiment observability.

## Validation truth and exact next Mac command

**Performed:** read the exact owner-provided terminal log; inspected current Stage111 runner and Metal code through GitHub connector; committed script error reporting. **Not performed:** owner's hidden per-trial `2048-direct.log` (not uploaded), rerun of the modified shell script, native GPU counter execution diagnostics, Vulkan/GLES runtime.

Before any further GPU work, the simplest action is to inspect the **existing** failed log (without rebuilding or running the GPU test):

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage111 || exit 1
ls -td build/batching-stage111/captures/capture-* | head -5
latest=$(find build/batching-stage111/captures -name '2048-direct.log' -type f -print | sort | tail -1)
echo "Log: $latest"
test -n "$latest" && tail -n 100 "$latest"
```

If the log shows `GPU model-pass counters UNSUPPORTED`, treat as a supported capability outcome: do not fake GPU tick results; use Xcode GPU Frame Capture or Instruments instead. If it shows `GPU model counter timeline INVALID`, investigate asynchronous completion sample buffer attachment, timestamp validity, or frame serial correlation before modifying GPU optimization. If Metal aborts/errors, inspect driver API and capability-specific conditions with targeted code change and detailed handoff. Only after an actual error cause is known rerun the corrected runner.

To use corrected runner on existing compiled Stage111 detached worktree without touching the checkout/rebase state:

```bash
git fetch origin perf/auto-batch-cost-metrics-20261119
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage111-trials.sh > tools/batching-stage111-trials-fixed.sh
bash -n tools/batching-stage111-trials-fixed.sh
AIVERSE_BATCH_BENCH_BIN=build/batching-stage111/aiverse_material_batches bash tools/batching-stage111-trials-fixed.sh
```

Do not claim success until the 66 model counter readings per run, whole GPU CB timestamps, actual MDI pixel parity and all native processes are verified.
