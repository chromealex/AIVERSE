# AIVERSE — Stage 130: balanced mesh workload trial order and CI gates

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Working branch:** `perf/batching-stage129-vertex-workload-20261120`  
**Draft PR:** [#75](https://github.com/chromealex/AIVERSE/pull/75), base `perf/auto-batch-cost-metrics-20261119` (Draft PR #39).  
**Source commits:** `0f0a47af3cd69d5eac660c8ead75d2066b113171` (interleaved test order), `5ebf376d25f4c8ee71259b2cd6efc165fbc69245` (macOS CI offline checks). This handoff is a subsequent documentation commit.

## Why Stage130 was needed

Stage129 introduced a 16-process benchmark comparing Metal CPU MDI versus Direct at 2048 constant model nodes with N=1 and N=8 triangles-per-edge tessellation (1 versus 64 triangles per model primitive), and pipeline inheritance OFF/ON. The original runner executed eight N=1 cases before eight N=8 cases. That would fully confound geometry level with time of measurement, including thermal/drivers/cache/run-order drift. Stage130 revises the runner to keep the same eight-mode counterbalanced Stage127 sequence while alternating the **order of N=1 and N=8 for each mode**. Full 16-run inventory, one-run-per-process, 66 whole-GPU-CB samples, 81 CPU measured frames, forced actual MDI command/submission counts and parity assertions are unchanged.

## Files modified

1. `tools/batching-stage129-trials.sh`: precreate `mesh1/` and `mesh8/`; iterate over eight OFF/ON Direct/MDI entries, interleave mesh variants `(1,8)` and `(8,1)` by entry index, then analyze both mesh directories once each. Manifest adds `mesh_order=interleaved_alternating_1-8_8-1_per_mode`. The exact expected mode/repeat naming, per-run logs and analysis paths remain unchanged. Shell arithmetic increments `caseIndex=$((caseIndex + 1))` to work with `set -e`.
2. `.github/workflows/gpu-batching.yml`: in the existing `metal-cpu-mdi` job, after compilation, add CPU-only `bash -n` on Stage129 runner, `python3 -m py_compile` for Stage127+129 analyzers, `python3 tools/batching-stage129-analyze.py --self-test`, and `aiverse_material_batches --unit`. Existing GPU raster checks are preserved. This job does **not** run Stage129's 16-process performance trials because shared GitHub macOS runners are unsuitable as comparable Mac Studio performance baselines.
3. `docs/agent-handoffs/batching-stage130-interleaved-ci-gates-2026-11-20.md`: this handoff.

No changes to `GfxMetal.mm`, Vulkan/GLES, shaders, CPU MDI admission/defaults, render paths, material code, GPU execution safety or any other system. Retain all AGX crash mitigations.

## Validation — careful distinction

**Actually performed by assistant:** fetched Stage129 and Stage127 implementation and current GitHub workflow, inspected C++ fixture and Metal CPU ICB code; verified the PR changed-file set and Actions workflow trigger; independently executed local Python math checks of the *algorithm* for N=1,2,4,8: exactly N² oriented triangles, same signed reference triangle area 0.5, and 2048*N² triangles in the stress fixture. Independently calculated the deterministic 16-run interleaving schedule and confirmed exactly eight N=1 / eight N=8 processes.

**Not performed:** run the actual checked-in Stage129 shell script via `bash -n`; run checked-in Python analyzer's `--self-test`; compile/run the fixture native executable; run 16 Metal GPU performance processes; verify actual pixel parity; collect new GPU timings; full CTest; Vulkan/GLES builds. Do not claim those as PASS on the basis of independent mathematical checks.

**GitHub Actions:** automatic CI and GPU batching focused regression were queued for commit `5ebf376d` when inspected, not yet completed. Inspect final HEAD's GitHub status before claiming any check passed. Workflow job URL can be found under PR #75 checks. Failed checks require first-error logs and a new handoff for fixes.

## Next native Mac Studio step

Use the untouched instructions in [Stage129 handoff](batching-stage129-vertex-workload-2026-11-20.md). Fetch updated branch, create a **new** detached worktree, compile `aiverse_material_batches` from this branch, then:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage129
bash -n tools/batching-stage129-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py tools/batching-stage129-analyze.py || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
build/batching-stage129/aiverse_material_batches --unit || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage129/aiverse_material_batches \
  bash tools/batching-stage129-trials.sh
```

The final `Stage129 archive: ...tar.gz` contains `stage127-runs.csv` and `stage127-comparison.csv` in both mesh subfolders, plus `stage129-summary.csv`, `stage129-scaling.csv` and PPM captures. The success criterion is **16 actual passing processes, 1056 completed GPU timestamps, 1296 measured CPU frames**, zero pixel change within each forced-MDI run, and invariant 18 submissions / 4086 commands per MDI frame. Counts are targets until native execution proves them.

## Pending interpretation / next agent

Compare *matched* Direct/MDI whole-command-buffer premium at N=1 and N=8 for inherited pipeline ON/OFF. Report both individual process medians (not just aggregates), P95, CPU wall and any thermal drift; do not infer isolated vertex shader GPU time. If MDI's excess stays roughly fixed while both workloads grow, suspect fixed ICB playback/state cost; if it grows disproportionately, investigate command encoding/vertex fetch/raster behavior with further controls. Results require real native device run before an optimization can be justified. Keep PR #75 Draft and avoid enabling production MDI/AUTO/inherited pipeline globally.
