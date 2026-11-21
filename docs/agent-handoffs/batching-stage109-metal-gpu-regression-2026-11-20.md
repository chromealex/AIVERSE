# AIVERSE Stage 109 — Metal GPU regression observed in Stage108; cross-order repeat

**Date:** 2026-11-20
**Repo:** `chromealex/AIVERSE` 
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft

## Owner-uploaded Stage108 archive: evidence and verified outcomes

The assistant inspected the exact uploaded `capture-20261120T074128Z-20243.tar.gz` in Python (25 tar entries including manifest, four native test logs, 4 CPU timeline CSVs, 4 GPU timeline CSVs, before/after PPM and MDI frame-100 PPM, and `stage108-summary.csv`). Manifest confirms source checkout `f0ba60f3c3caf867156d762936127087e945e25c`, `result=PASS`, UTC 07:41:28–07:41:44, order **2048-direct / 2048-mdi / 4096-direct / 4096-mdi**. Native Mac Studio logs all show **38 CPU unit checks PASS**, end-to-end before/after images byte-identical, MDI actual frame 100 parity `changed_channels=0` and real 81/81 MDI frames when selected, with 1458/2916 real submissions at 2K/4K respectively. All four GPU CSVs have **66 valid timestamps with unique tags 115..180**, measured on completed Metal command buffers with `GPUStartTime`/`GPUEndTime`.

| Load | Mode | GPU command-buffer median ms | GPU P95 ms | GPU maximum ms | CPU render() median ms | CPU P95 ms |
|---|---|---:|---:|---:|---:|---:|
| 2048 | Direct | 0.581500 | 0.638792 | 0.905208 | 2.30367 | 2.36979 |
| 2048 | CPU MDI | 0.856646 | 0.886583 | 0.920667 | 1.71738 | 1.80617 |
| 4096 | Direct | 0.999292 | 1.007630 | 2.765790 | 4.64508 | 4.77904 |
| 4096 | CPU MDI | 1.403790 | 1.463580 | 2.814750 | 2.96950 | 3.13383 |

Single-process GPU median change **+47.32%** at 2K and **+40.48%** at 4K for MDI vs Direct, while the CPU `render()` medians drop **25.45%** at 2K and **36.07%** at 4K. The 4K GPU max values ~2.8ms are isolated spikes (P95 near 1.0/1.46ms). This is a credible **whole-command-buffer GPU timeline regression** in this synthetic scene, **not proven model-pass-only GPU work, occupancy/utilization, power, FPS or GPU frame pacing**. Never claim CPU gains directly improve FPS. Only one process per combination was captured in Stage108, with order not counterbalanced.

**Key engineering conclusion:** do not default-enable CPU MDI or merge PR #39 on CPU results alone. Investigate Metal ICB GPU execution overhead, uniform binding/state paths and whole-frame scheduling with repeatable data, preserving correctness and safety rules; avoid removing mandatory resource residency/bindings without evidence.

## Stage109 code / API changes

**Commits:**
- `97d06caa4f50189789fdad8de900fd50119a4ac9` adds `tools/batching-stage109-analyze.py`
- `b9ce131419c1e04047ee4bea6cb9d187b7cbcf9b` adds `tools/batching-stage109-trials.sh`
- This handoff in its own GitHub commit.

**No C++ engine, Metal backend, policy, camera, RHI API or test fixture code changed in Stage109.** Existing Stage108 binary can be reused. Runner executes **Direct1 / MDI1 / MDI2 / Direct2**, for 2K and 4K (8 independent process runs), preserving native CPU invariants, real MDI frame-100 pixel parity, final image parity, all 66 valid GPU command-buffer samples and 81 CPU samples in each run. It produces a unique UTC/PID capture directory, provenance manifest, full per-run logs and CSVs, `stage109-summary.csv` and a `.tar.gz` bundle. Analyzer validates every timestamp and CPU command mode then compares GPU median of two independent per-process medians for each mode; P95/max and CPU summaries are recorded. It does not attribute the GPU CB time to model passes or eliminate all thermal/order effects.

## Validation truth

**Actually performed:** read actual Stage108 archive, parsed four GPU/CPU CSVs, validated all raster captures and native logs, identified numerical GPU median regression; GitHub added Stage109 runner and analyzer and this documentation. **Not yet performed:** Stage109 `bash -n`/Python syntax and native process execution on Mac Studio, GPU model-only time, Instruments GPU frame capture, Vulkan/GLES/GPU parity or actual-game scene validation. Branch stays Draft; opt-in settings unchanged.

## Safe owner Mac Studio follow-up — no rebuild

Use prebuilt Stage108 binary and detached Stage108 worktree. Fetch only the two Stage109 scripts from remote without changing Git checkout or any other agent's worktree/rebase state.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage108 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage109-trials.sh > tools/batching-stage109-trials-local.sh || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage109-analyze.py > tools/batching-stage109-analyze.py || exit 1
bash -n tools/batching-stage109-trials-local.sh || exit 1
python3 -m py_compile tools/batching-stage109-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage108/aiverse_material_batches \
  bash tools/batching-stage109-trials-local.sh
```

Send the printed `Stage 109 archive:` path as uploaded file, or complete summary + failed native log. If repeated GPU regression is confirmed, next isolate Metal model-pass GPU work using device-supported counter samples / Metal GPU frame capture, then compare CPU MDI and Direct while preserving the same pipeline, shader work and pixel parity. Do not infer full FPS/GPU speedup from CPU-only data.
