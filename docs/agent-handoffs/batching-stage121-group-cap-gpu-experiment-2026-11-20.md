# AIVERSE — Stage 121: CPU MDI grouping-size vs Metal GPU command-buffer cost

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft

## Problem and grounded baseline

Owner Mac Studio Stage119 native archive (Stage120 handoff `349599a93670f24f799d64bc03bb0c620f273e78`) confirms Direct vs CPU MDI GPU whole-command-buffer regression at 2048 unique primitive stress under all three workload variants:

| 2048 variant | Direct GPU CB median | CPU MDI GPU CB median | Change for MDI |
|---|---:|---:|---:|
| Full shadow+opaque | 0.574354 ms | 0.808917 ms | +40.84% |
| Shadow models only | 0.176771 ms | 0.336875 ms | +90.57% |
| Opaque models only | 0.585563 ms | 0.736646 ms | +25.80% |

The fixture also records substantially lower synchronous CPU render wall time for MDI and full raster parity across both modes. None of those numbers represent model-only GPU timestamps or actual FPS. The unavailable draw-boundary Metal counters and crashing Xcode GPU Replayer prevent trustworthy per-pass GPU timing on this hardware.

**Hypothesis to test, not established claim:** fixed ICB-per-submission GPU cost could explain some regression. To separate group count from command count, cap MDI command groups *only in the diagnostic fixture*, keep all 2048 authored unique primitives, Metal shaders, material/scene settings and safety bindings unchanged, and compare whole-GPU-CB times while monitoring actual submitted group counts and command counts.

## Source changes

1. `engine/render/Renderer3D.h`, commit `75672086223ad0f2b99f9fccbfd23d074fe5885e`: new **diagnostic-only** `setDiagnosticCpuMdiGroupCap(uint32_t cap)`. Zero preserves default `instanceLimit_` (current effective Metal group size ~227); nonzero bounded to 16..256. Separate member `diagnosticCpuMdiGroupCap_=0`. This is intentionally NOT a new graphics quality/production option.
2. `engine/render/Renderer3DModels.cpp`, commit `0965f88457bab4108f3c62a07c3fdac2189d9126`: the CPU MDI compatible-run scan caps `end-i` at `min(instanceLimit_,diagnosticCap)` when nonzero; other paths still use the original `instanceLimit_`. This does not recompile shader instances, modify model uniform slot capacity, change GPU ICB buffer residency, protected parent/child bindings, compute or render pass state. Ineligible/short runs still use existing validated Direct fallback.
3. `tests/render/material_batches.cpp`, commit `0ff56d54adc2469c2c84ba24fa03de7232bb9f1f`: stress-only optional `AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP=0|16..256`, parsed with explicit rejection of invalid values; setter applied at fixture initialization, before baseline capture and subsequent forced-mode draws. Logs `stress diagnostic CPU MDI group cap=N`. When env absent defaults to 0, preserving existing test behavior.
4. `tools/batching-stage121-analyze.py`, commits `20a43ce4bf04e2f4290041d3a0e941130fd577f0` and `7f02bcc9673c55729222d39667c054f59d9af942`: validate ten full-CPU/GPU CSV sets (66 GPU timestamp frames 115..180 and 81 measured CPU frames 115..195 each), mode, fixed MDI submission/command counts and >90% authored two-pass work coverage. Report per-trial GPU CB median/P95/max, CPU render median/P95, actual MDI submissions and commands; compute mode medians across mirrored A/B runs. **GPU metric is whole completed command-buffer time, not isolated GPU model-pass duration or FPS.**
5. `tools/batching-stage121-trials.sh`, commits `22f2b3f40790edc43e7f592b94ac07aa44212ca9` and `7608ced879a4c21b92df6a2f24be16c3aeac2724`: 2048-primitive native process order **Direct1, cap32a, cap64a, cap128a, defaulta, defaultb, cap128b, cap64b, cap32b, Direct2**. This controls one-way warmup/drift to a degree, but is not randomized per frame. Eight forced MDI+two Direct runs, each with 66 valid whole-CB GPU timestamp samples and 81 CPU measured frames. Disable inherited AUTO, GPU ICB, Metal trace/counters and batch benchmark mode flags; preserve native CPU 38 checks, real-MDI frame-100 pixel parity, final Direct raster parity, fixed workload checks. Output unique UTC+PID capture directory and `.tar.gz` on success, or failure log plus partial archive through ERR trap.
6. This handoff document in its own commit.

## Distinguishing mechanics from confounds

- The size limit changes CPU MDI group segmentation without changing `setInstanceLimit`, so the shader-uniform capacity remains constant and the same primitive set should render; it may change residual Direct fallback tail counts. Always check actual MDI commands (candidates not equal authored count) and byte-identical raster.
- A GPU change at smaller caps can be caused by more ICB dispatch overhead, resource declarations, or the GPU driver’s scheduling/ICB encoding. The experiment alone cannot identify which. If the same total indirect commands are divided into more groups and GPU time grows, a per-group cost is a candidate.
- 0 cap means original grouping, not zero commands. Metal `AIVERSE_METAL_MDI_MIN_COMMANDS` remains at default 16 for all trials.
- 10 independent process comparisons still have environment/clock variance. If 0 cap is fast vs 32 cap, investigate group overhead but do not assume causal optimization without repeated hardware tests and GPU capture. In all cases, keep CPU MDI default-off and PR Draft pending GPU regression resolution.

## Validation truth

**Performed:** GitHub source inspection and commits to renderer diagnostic setter, MDI grouping scan, test fixture, runner, Python analyzer; preserved hard parity and mode checks. **NOT performed:** actual native AppleClang build after these changes, ten-run Mac GPU experiment, Python analyzer execution with Stage121 CSVs, Vulkan/GLES builds, GPU model-only counters, real game performance. Previous Stage119 tests belong to earlier code and do not validate the current code. No production performance improvement is claimed.

## Mac Studio next step — safe isolated worktree

Avoid resetting or switching any worktree with a pending rebase/merge. Build in a new detached checkout, then run the 10-case experiment:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage121
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage121" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage121" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage121-trials.sh || exit 1
python3 -m py_compile tools/batching-stage121-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage121/aiverse_material_batches \
  bash tools/batching-stage121-trials.sh
```

If the runner passes, upload `Stage 121 archive: ....tar.gz`. If a failure occurs, inspect the last 70 lines of the current trial log printed by the trap and share the failure archive. The runner is careful not to turn unsupported GPU hardware-counter sampling back on; it uses the previously verified completed whole-CB GPU timestamps.

## Next agent

Parse the native ten-run archive, compare per-cap GPU CB median/P95, CPU median, real submitted groups and commands, and pixel parity. Evaluate whether GPU overhead scales with count of ICB groups or with total indirect commands; if no correlation, investigate other shared Metal ICB GPU costs without removing crash-workaround pipeline/buffer bindings or residency safeguards. After any next stage, write a separate handoff with actual tests, branch/commit, limitations and next concrete step.
