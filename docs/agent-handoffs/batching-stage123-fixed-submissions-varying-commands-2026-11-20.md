# AIVERSE — Stage 123: fixed MDI submission count, increasing commands

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Changes:** `tools/batching-stage123-analyze.py`, `tools/batching-stage123-trials.sh`, this handoff. No production C++/Metal/RHI/AUTO changes.

## Goal and previous verified work

Stage122 analyzed the user's ten-process Stage121 native Mac Studio capture `capture-20261120T093029Z-73785.tar.gz`. At 2048 unique primitives, Direct whole Metal GPU command-buffer median was ~0.5704ms, uncapped CPU MDI ~0.8155ms (+43%). Capping MDI groups to 32/64/128 raised submissions per measured frame to 128/64/32, with GPU medians ~0.8638/0.8467/0.8162ms. Thus reducing ICB submissions from 128 to 18 yielded only ~5.6% whole-CB improvement and did not eliminate the ~43% MDI GPU regression. Native pixel parity and 38 CPU checks passed in the owner's archive, but real GPU model-pass counters and Xcode Performance profiling are unavailable/crash on this device. No tested GPU-specific root cause.

**New falsifiable hypothesis:** GPU regression may scale with the number of indirect commands, even when the number of ICB submissions is held constant. Unlike Stage121, which changed submission count at fixed workload, Stage123 changes authored scene size and indirect commands while aiming to keep ICB **submission count at 64**. This is a *correlational workload scaling test*, **not** an isolated causal per-command benchmark because different scene sizes also change geometry/shading work, shader memory accesses, resources and CPU load.

## Test matrix

| Unique primitives | Explicit diagnostic group cap | MDI submissions / measured frame | Indirect commands / measured frame |
|---:|---:|---:|---:|
| 1024 | 32 | 64 | 2048 |
| 2048 | 64 | 64 | 4096 |
| 4096 | 128 | 64 | 8192 |

The expected 64 submissions are 32 for shadow plus 32 for opaque. This is a **predicted test invariant**; the analyzer intentionally aborts if real counts differ. Direct reference processes always require zero indirect submissions/commands, with one draw per authored model primitive for each model pass. For each size, run **Direct1 → MDI1 → MDI2 → Direct2**, giving 12 native processes, six forced MDI, six Direct. Each process records 81 measured CPU frames (115..195) and 66 completed Metal whole-command-buffer GPU timestamps (115..180).

## Implemented code and APIs

- `tools/batching-stage123-trials.sh`, commit `7784fe1b083632b670f89500a10486f94a839d48`: runs the 12 scenarios in a new UTC/PID capture folder; uses the **prebuilt Stage121 fixture** and its existing `AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP` flag, default full shadow+opaque model workload, and disables inherited AUTO, GPU-encoded ICB, GPU frame trace, unsupported draw-boundary counters and slow logging. Requires the fixture's 38 CPU invariant PASS, terminal raster `changed_channels=0`, actual MDI capture parity on MDI processes, 66 valid GPU timestamps, 106 CPU timeline frames, 81 real mode-specific measured frames, and **5184** total measured MDI submissions (64 × 81) in each forced MDI process. On a failure prints the latest 75 lines of the per-run log and saves a **partial archive** rather than claiming success. On success creates one `.tar.gz` with 12 native logs, CPU/GPU frame CSVs, PPM captures, manifest and analysis output.
- `tools/batching-stage123-analyze.py`, commit `dae5298f91e2f9014223fcefd88c051a8b7425dc`: validates exactly 66 valid GPU timestamps and 81 measured CPU frames per process, actual Direct/MDI mode, exactly **64** submissions, **2×items** MDI commands, stable per-frame workload and 32 per-pass model draw groups for each MDI scenario. Reports per-process whole Metal GPU CB median/P95/max and CPU wall median/P95; reports median of two process medians per mode and percent/difference GPU premium over matched Direct for each workload. Writes `stage123-runs.csv` and `stage123-comparison.csv`. `premium_per_command_ms_descriptive_only` is **only an arithmetic normalization**, not measured marginal GPU cost. Python output expressly warns about this confounding.
- This handoff file is a separate commit. **No C++ fixture or production source change** was required: Stage121 had already added a test-only group cap independent of shaders and Metal binding state.

## Validation truth

**Performed this stage:** read actual Stage121 renderer fixture, previous runner/analyzer, branch and Draft PR state through GitHub connector; committed both scripts; source-level cross-checked expected mode labels, 1K/2K/4K matrix, 64×81 submission invariant, and availability of fixture flags. **NOT performed:** native run of the new 12-case shell script, actual `bash -n` or Python bytecode compilation, on-Mac 1K cap32/4K cap128 expected 64 count, new GPU timing, Vulkan/GLES tests, full CTest or cross-device scene tests. Previous Stage121 owner results do **not** validate this script. If a native assertion fails, inspect native log + CSV first and only revise an expected group count after evidence, without weakening raster or GPU validity checks.

## Run on existing Mac Studio Stage121 worktree without rebuild or rebase

Earlier detached worktrees may have in-progress rebases; do not `git reset --hard` or `git switch`. Fetch **just** the new scripts into the Stage121 detached worktree; the compiled Stage121 binary already supports diagnostic group caps and GPU completion timings:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage121 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage123-trials.sh \
  > tools/batching-stage123-trials-local.sh || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage123-analyze.py \
  > tools/batching-stage123-analyze.py || exit 1
bash -n tools/batching-stage123-trials-local.sh || exit 1
python3 -m py_compile tools/batching-stage123-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage121/aiverse_material_batches \
  bash tools/batching-stage123-trials-local.sh
```

Share generated `Stage 123 archive: ...tar.gz` or the emitted summary. The runner never edits the engine, touches other worktrees or uses Xcode GPU Replayer. Keep the user's existing Stage121 captures intact.

## Next-agent interpretation and continuation

Compare MDI GPU premium (MDI minus matched Direct whole-CB time) across 2K/4K/8K indirect commands at equal 64 GPU ICB submissions. If premium **rises substantially** with command count, per-command ICB execution or scene-dependent vertex/shader work is a candidate; if premium remains approximately constant, a shared per-frame/ICB setup cost or state scheduling is more plausible. This alone does **not** prove cause, because both variants execute different numbers of geometry primitives. Only alter optimized Metal ICB code after additional targeted evidence and preserve mandatory explicit pipeline/buffer state, residency declarations and shader correctness. Default Metal CPU MDI and AUTO remain opt-in/OFF; PR #39 Draft.
