# AIVERSE — Stage 128: fix Stage127 OFF/ON analyzer; first owner GPU results

**Date:** 2026-11-20
**Repository/branch:** `chromealex/AIVERSE`, `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft

## Owner validation input

User Mac Studio compiled `aiverse_material_batches` at Stage127 HEAD, then ran `tools/batching-stage127-trials.sh`. All eight tests printed PASS:

- OFF Direct1, OFF MDI1, ON Direct1, ON MDI1, ON MDI2, ON Direct2, OFF MDI2, OFF Direct2.
- Measured frame topology: Direct submissions=0 commands=0; MDI submissions=18 commands=4086 for each process.
- Runner's native checks for 38 CPU invariants, 66 valid GPU timestamps per process, image parity and measured real MDI successes all passed before the final Python analysis step.
- Python analyzer failed after printing OFF aggregate: `ValueError: high: unmatched mode controls` at `for quality in ("off","high"):`. This was a leftover Stage125 label; the actual Stage127 runs and `ORDER` use `off` and `on`. The runner archived all raw results at `build/batching-stage127/captures/capture-20261120T100208Z-84140.tar.gz` and the underlying directory still exists in the worktree.

Native per-process **whole GPU CB** medians (ms), user log:

| Pipeline mode | Direct1 | Direct2 | MDI1 | MDI2 |
|---|---:|---:|---:|---:|
| OFF | 0.564813 | 0.575626 | 0.814625 | 0.824625 |
| ON | 0.572458 | 0.567250 | 0.781583 | 0.782896 |

Medians of two process medians:
- OFF Direct 0.5702195 ms; OFF MDI 0.819625 ms; GPU premium 0.2494055 ms (+43.74%).
- ON Direct 0.569854 ms; ON MDI 0.7822395 ms; GPU premium 0.2123855 ms (+37.27%).
- ON vs OFF MDI whole GPU CB median −0.0373855 ms (−4.56%). MDI GPU premium vs matched Direct improves −0.0370200 ms (~−14.84% of former premium).
- This is **early evidence**, with only two independent processes for each mode. It does not mean MDI outperforms Direct on GPU; ON remains ~37.3% worse. Also, no claim of isolated model-pass time, GPU counter or real game FPS.

## Actual code change

**Commit:** `516c6b00b8427949bcc3c77ce7d03df88f09bdc7`.
**File:** `tools/batching-stage127-analyze.py`.
Change aggregate loop from `for quality in ("off","high"):` to `("off","on")`, and label summary `PIPELINE` to correctly describe inherited-pipeline mode. No renderer/RHI/Metal backend, shader, buffer bindings, residency, shader quality or CPU MDI defaults changed in this stage. Stage127 experimental Metal option `AIVERSE_METAL_CPU_MDI_INHERIT_PIPELINE=1` remains OFF unless explicitly enabled, and it retains explicit per-command vertex/fragment buffer slots.

## Local recovery without re-running GPU

Use **the existing Stage127 worktree and captured raw CSVs**:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage127 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage127-analyze.py \
  > tools/batching-stage127-analyze.py || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py || exit 1
python3 tools/batching-stage127-analyze.py \
  build/batching-stage127/captures/capture-20261120T100208Z-84140 || exit 1
```

On success, script writes `stage127-runs.csv`, `stage127-comparison.csv` into the existing captured folder. The previous tar.gz was generated **before** repaired analysis, so it does not automatically contain these new CSVs. If needed, regenerate the archive (after analyzer success):

```bash
tar -czf build/batching-stage127/captures/capture-20261120T100208Z-84140-fixed.tar.gz \
  -C build/batching-stage127/captures capture-20261120T100208Z-84140
```

## Validation truth / next step

**Performed:** inspected GitHub Stage127 analyzer, identified actual broken `("off","high")` branch, fixed and committed it, computed aggregate statistics directly from user-supplied log. **Not performed:** assistant-side native build, actual repaired Python execution against owner's CSVs, independent raw-archive verification, another repeated GPU experiment or Vulkan/GLES checks. The user performed eight native runs and runner checks; corrected analyzer still requires local execution.

**Next:** rerun just Python analysis as above; then repeat counterbalanced ON/OFF test on representative geometry, not only synthetic 2048 primitives, and inspect whether reduced GPU CB time persists. Do not enable CPU MDI or pipeline inheritance globally until synthetic and representative workloads demonstrate acceptable GPU behavior. PR #39 remains Draft.
