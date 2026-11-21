# AIVERSE — Stage 125: fixed geometry and ICB topology, Low vs High material shader quality

**Date:** 2026-11-20
**Repository / branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Stage commits:** `62d9f1b4f65c02aaa9e8ed7a490ebae3f6cb0d3d` analyzer, `8c032984078a71831dc4623e86529c88cbda69d0` runner, this handoff.
**Changed paths:** `tools/batching-stage125-trials.sh`, `tools/batching-stage125-analyze.py`, this file.
**No changes** to C++, Metal ICB, shader code, buffer bindings, residency, frame fences, Vulkan/GLES or CPU MDI defaults.

## Evidence and purpose

Stage124's verified user Mac Studio capture from Stage123 (`capture-20261120T093909Z-77250.tar.gz`) demonstrates a whole-completed-Metal-CB GPU premium of +50.4%, +48.2%, +50.8% for CPU MDI over Direct at 1K/2K/4K unique primitives. It remained as 2K/4K/8K indirect commands grew while MDI submission count was held at 64. Because scene complexity grew, exact per-command hardware cost is not isolated.

**Goal:** change **material quality** rather than number of primitives or MDI group sizes, observing whether MDI's whole GPU command-buffer premium persists across built-in shader permutations while render geometry and ICB topology stay constant. The fixture already supports `AIVERSE_PALETTE_TEST_QUALITY` with enum `render::MaterialQuality::Low=0` (Blinn-Phong/base-color) and `High=2` (GGX/PBR material maps, occlusion). These are existing renderer paths; if the fixture's actual material textures/pipelines fail to exercise the High shader branches, a small observed change **does not prove** shader complexity had no role. We do not measure actual shader instruction count, GPU occupancy or a pass-only duration.

## Workload and source

New scripts use **the existing precompiled Stage121 benchmark binary**. 2048 authored unique primitive models, 2 model passes, 2048 direct draws per pass (4086 indirect commands + 10 direct remainder when default CPU MDI grouping is active). Runner process order:
`Low Direct1 → Low MDI1 → High Direct1 → High MDI1 → High MDI2 → High Direct2 → Low MDI2 → Low Direct2`.

Thus eight independent processes: 2 Direct and 2 MDI for each shader quality, with a mirrored order to reduce monotonic drift confounding. Each process records 66 completed asynchronous GPU CB timestamps (frames 115..180) and 81 measured CPU render frames (115..195). GPU timestamp is **whole completed command buffer**, not shader-only time or FPS.

- `tools/batching-stage125-trials.sh`: runner sets `AIVERSE_PALETTE_TEST_QUALITY=0` or `2`, `AIVERSE_BATCH_STRESS_POLICY=direct|mdi`, `AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS=full`, `AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP=0`, 2048 static unique primitives, asynchronous GPU CB timestamps and CPU timeline. Explicitly unsets inherited GPU-encoded ICB, AUTO, trace, unsupported model GPU counters and `AIVERSE_MATERIALS` override. Checks 38 fixture CPU invariants PASS, end raster `changed_channels=0`, MDI frame-100 real path parity, measured 1458 MDI submissions for 81 measured frames (18/frame), complete 66/106 timelines. On any test failure, produces a partial archive and native log tail instead of reporting a GPU result.
- `tools/batching-stage125-analyze.py`: validates exact frame sequences, GPU timestamps valid and positive, CPU render time, per-frame unchanged draw topology. Requires each Direct test `(mdi_submissions,mdi_commands,shadow_draws,opaque_draws)=(0,0,2048,2048)`, each MDI test `(18,4086,14,14)`. The identity of this draw topology across both Low and High is a **critical test invariant** — if material quality alters MDI admission/grouping, fail and revisit the experiment instead of comparing different topologies. Computes run-level median/P95/max GPU CB and median/P95 CPU render wall, two-process median of medians per mode/quality, GPU MDI difference (ms and %) at both qualities. Writes `stage125-runs.csv` and `stage125-comparison.csv`. The order/validations preserve raster parity *within each quality/test run*, not pixel identity of Low versus High shader permutations (which would be incorrect to require).

## Validation truth and limitations

**Performed:** GitHub source inspection of `render::MaterialQuality`, existing fixture `AIVERSE_PALETTE_TEST_QUALITY` handling, its CPU/GPU timeline CSV schema, model draw logs and Stage121/123 runner; committed new scripts to PR #39 feature branch; reasoned through expected topology and mode flags. **NOT performed:** native shell execution of Stage125 runner, Python bytecode compile on user's Mac, new completed GPU measurements, macOS Metal shader comparison, full CTest suite, Shader instruction-count validation, representative game scene, Vulkan/GLES timing or improvement. Stage123 prior user results are separate and do not certify Stage125.

**Limitations:** Low vs High changes shader code and potentially material texture accesses/pipeline caching, so the measured GPU delta cannot be attributed to instruction cost alone. Within-quality raster parity guards renderer correctness. GPU driver variability and shaders' material branch selection remain confounds. If MDI submission count is not exactly 18/frame on Low or High, fail and inspect the log; do not relax the check blindly.

## Reuse existing native binary (no C++ rebuild)

Run without changing the worktree HEAD or touching any other agent branch:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage121 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage125-trials.sh \
  > tools/batching-stage125-trials-local.sh || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage125-analyze.py \
  > tools/batching-stage125-analyze.py || exit 1
bash -n tools/batching-stage125-trials-local.sh || exit 1
python3 -m py_compile tools/batching-stage125-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage121/aiverse_material_batches \
  bash tools/batching-stage125-trials-local.sh
```

On success share `Stage125 archive: ...tar.gz`. The scripts save under `build/batching-stage121/stage125` and preserve old Stage121/123 results. On fail share the failure log and partial archive. Do not use Xcode GPU Performance replayer, which crashed on owner's Xcode 26.3, or unsupported draw-boundary counter API.

## Next agent step

Analyze eight native logs and all valid timestamp/CPU CSVs from owner archive. Decide whether MDI premium relative to Direct varies materially under the two same-geometry shader-quality variants, being careful that high-quality shader work may not be engaged by fixture's materials. If negligible, further distinguish ICB per-command setup and execution resource cost via shader-independent or hardware-targeted tests; do not disable explicit Metal pipeline/vertex/fragment bindings or residency. If materially different, inspect shader/material variant and per-command explicit ICB state for unnecessary GPU work. Keep CPU MDI/AUTO default OFF, PR #39 Draft. Record every subsequent code stage in a fresh agent handoff.
