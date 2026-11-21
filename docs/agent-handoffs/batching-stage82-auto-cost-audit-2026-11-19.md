# AIVERSE batching — Stage 82: AUTO wall-time cost audit

**Date:** 2026-11-19
**Owner branch:** `perf/auto-batch-cost-metrics-20261119` (created from `main` after merged PR #34)
**Worktree safety:** the owner's main checkout `/Users/aleksandrfeer/Projects/AIVERSE` has unresolved `UU AGENTS.md` and local changes to Vulkan/docs. Do not use `pull`, `reset --hard`, `rebase --abort`, or merge into that checkout. Use a **separate detached worktree** as directed below.
**Code commits:** `695ffceae4f8be475ba1e526a0dd1ad73dbf49ad` and `cd05c38fd6efb7441a5d72b4dac0785cb0fe1176` in `engine/render/Renderer3DModels.cpp`.
**Validation:** GitHub commit creation/source examination only; native Mac compile, raster and GPU tests **not performed** for Stage 82. Production flags/default behavior unchanged.

## Background and evidence

PR #34 was successfully merged into `main` at `58e29ab052458fffcdebebffe27022b53adb9367`. Earlier owner Mac Studio Stage 80 runs (2 process-order-rotated cycles) passed all six fixed-camera trials with 35 original + 3 scheduler CPU checks expected under Stage 81. The **observed** `renderer.render()` CPU medians on 1,024 unique primitives were:

| Path | Cycle 1 (ms) | Cycle 2 (ms) |
| --- | ---: | ---: |
| Direct | 1.1545 | 1.1685 |
| Forced CPU MDI | 0.8846 | 0.8772 |
| AUTO | 1.1464 | 1.1479 |

Both forced CPU MDI runs made 810 CPU MDI submissions during 81 measured batched frames, with `actual MDI pixel parity frame=100 changed_channels=0` and terminal `changed_channels=0`; both forced Direct runs had zero MDI submissions. AUTO still selected Direct at end; its per-shadow/opaque MDI EWMA values were inexplicably higher than corresponding Direct EWMAs despite the lower overall forced-MDI `renderer.render()` median. CPU pass metrics and complete render wall time have different measurement scope, so these numbers do not yet prove that the scheduler makes a mathematically incorrect choice. Stage 81 increased MDI calibration probes but its new native AUTO outcome has **not yet been provided**.

## Scope and architecture

`Renderer3D::renderModels(phase)` already measures one CPU phase from `passBegin`; after completion, `AutoCpuBatchPolicy::observe` stores the raw synchronous wall duration in a pass/size bucket. Stage 82 adds a flag-gated trace **at the exact observation point**, printing the sampled phase, mode selected by real MDI submissions, number of items, raw CPU pass duration, per-pass completed MDI submissions, current Direct/MDI EWMA, sample counts, and opportunities.

- Flag: `AIVERSE_BATCH_AUTO_COST_TRACE=1`. Default OFF. Prints at most 24 samples per mode and phase per process (96 maximum) to avoid spamming every frame.
- Format: `[auto-cpu-cost] phase=0 mode=mdi items=1024 wall_ms=... submissions=... direct_ewma_ms=... mdi_ewma_ms=... direct_samples=... mdi_samples=... opportunities=...`.
- The instrumentation does **not** change the selector, timing boundaries, geometry compatibility, the Metal RHI, or GPU synchronization.
- This is **CPU wall time**, not GPU completion latency. Trace I/O can distort timing on subsequent frames; compare this diagnostic run with an untraced run, not as an authoritative performance benchmark.

## Exact Mac commands (no modifications to conflicted checkout)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage82" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage82" --target aiverse_material_batches --parallel 8 || exit 1
mkdir -p "$WT/build/batching-stage82/captures"
(
  cd "$WT" || exit 1
  AIVERSE_PALETTE_TEST_CASE=auto_stress \
  AIVERSE_BATCH_STRESS_ITEMS=1024 \
  AIVERSE_BATCH_AUTO=1 \
  AIVERSE_BATCH_CPU_TIMING=1 \
  AIVERSE_BATCH_AUTO_COST_TRACE=1 \
  AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
  build/batching-stage82/aiverse_material_batches \
    unused.obj build/batching-stage82/captures/auto-stage82-1k \
    2>&1 | tee build/batching-stage82/auto-stage82.log
)
```

If detached-worktree path already exists, inspect it rather than overwriting it. Do not suppress compile/test errors. For this run require CPU checks PASS, a verified actually-MDI capture `changed_channels=0`, final Direct-vs-AUTO screenshot parity, and exit code 0. Because `tee` can hide program exit codes without pipefail, check the log's final result and consider `set -o pipefail`.

## Next agent tasks

1. Compare the 24 raw MDI and Direct per-pass times with `successful MDI CPU stage` and complete `renderer.render()` distributions. Investigate any gap, including phase invocations per frame, early GPU waits, and omitted CPU initialization.
2. Only if a measurement boundary or cost-model bias is demonstrated, correct it with a targeted regression fixture. Do not introduce another unsubstantiated preference threshold.
3. Rerun six 1k trials (rotated Direct/MDI/AUTO); capture real CPU MDI pixel parity and GPU timestamps when supported. Revisit 10k only afterward.
4. Record native results, new commits, limitations and next step in **another handoff**. Keep automated flags OFF by default and create a new Draft PR after passing tests. Preserve the owner's unrelated worktrees.


---

## Stage 83 — owner Mac Studio validates Stage 82 AUTO calibration; traced run slows down (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (PR #39 Draft). **Source tested:** Stage 82 head `ad20f0b569064d29090f9716fedb016c02bf8b9c`. **This stage reports actual user-provided native test results; no further runtime changes are verified yet.** Target worktree was `/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82`, detached at the correct head; primary checkout remained untouched.

### Actual owner-provided Mac Studio build/test evidence

- AppleClang 17.0.0: configured the separate worktree successfully and built the real `aiverse_material_batches` including `Renderer3DModels.cpp` and `GfxMetal.mm` without a compile/link error.
- `material palette CPU checks=38 result=passed`.
- Synthetic geometry: 1024 authored unique primitives, one ModelDraw.
- Confirmed **actual MDI-submitting frame 101** `changed_channels=0` versus Direct baseline; final AUTO frame also `changed_channels=0`.
- `stress benchmark policy=auto measured_mdi_frames=80 measured_direct_frames=1 measured_mdi_submissions=730`; terminal `batch=1 shadow=5 opaque=5 total=12 error=`, 10 real CPU MDI submissions and 2048 indirect commands. Cumulative MDI scans=attempts=submitted=860, 0 failed/ineligible/geometry/material/state break rejections.
- `Stage 82 exit=0`.
- `AUTO CPU learning phase=0 bucket=2 direct=18 0.92662ms mdi=88 0.79663ms opportunities=106`; opaque `direct=22 0.90724ms mdi=84 0.71809ms opportunities=106`. **The CPU selector now chooses MDI on the test and is no longer stuck on Direct.**

CPU render timing recorded from this specific highly traced run:

| Sample | Count | Median (ms) | P95 (ms) |
| --- | ---: | ---: | ---: |
| Direct baseline | 66 | 1.1740 | 2.5262 |
| mixed AUTO | 81 | 1.4177 | 2.1721 |
| AUTO Direct fallback | 8 | 1.1988 | 1.2741 |
| AUTO MDI-submitted | 88 | 1.3492 | 2.1721 |
| AUTO late frames | 36 | 1.7112 | 2.2683 |

Sampled successful-MDI CPU stage mean per pass: shadow `total=0.62334ms submit=0.39444ms` (n=83 frames, 415 submissions); opaque `total=0.51661ms submit=0.38189ms` (n=79, 395 submissions). Last frame `submit=0.50567ms` shadow and `0.55725ms` opaque.

### What the detailed diagnostic actually demonstrates

At cold startup, first MDI passes were **extremely expensive** (phase0 39.80017ms, phase1 444.91758ms). The next MDI pair measured 1.73/1.61ms. By opportunities ~8–36 the completed MDI passes largely reached ~0.3–0.4ms against initial Direct ~0.5ms, and MDI EWMA dropped below Direct; 32+ MDI calibration samples allowed the policy to overcome the cold EWMA. Late in the run there were substantial >1ms pass-time spikes for both paths, so the overall AUTO wall time was slower than previous untraced six-run fixed-scene CPU MDI runs, whose forced MDI medians were 0.8846ms and 0.8772ms. **The trace run does not establish that AUTO itself causes slowdown**: debug `fprintf(stderr)` on many early frames, cold caches, GPU submission/synchronization and time-varying external contention can affect wall timing. No GPU timestamps, frame thermal/frequency metrics, or randomized per-frame crossover were collected. Do not use this diagnostic sample to tune the 8% policy threshold or claim default-on is ready.

### Next exact step: retrace-free counterbalanced test

From the isolated worktree, use its retained Stage 80 rotated runner **without** `AIVERSE_BATCH_AUTO_COST_TRACE` or `AIVERSE_ICB_PROFILE`. The mode-specific runner already verifies forced CPU MDI raster parity and actual submissions. A fresh six-run log under `build/batching-stage82/stage83-clean` is needed to measure the genuinely learned AUTO after Stage 81 policy changes under balanced process order. Compare both Direct and forced MDI results before drawing conclusions; if all three modes slow simultaneously, suspect device/environment rather than AUTO alone.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
unset AIVERSE_BATCH_AUTO_COST_TRACE AIVERSE_ICB_PROFILE AIVERSE_BATCH_AUTO_TRACE
bash tools/batching-stage80-ab.sh 1024 build/batching-stage82/stage83-clean
```

The script uses `AIVERSE_BATCH_BENCH_BIN` default `build/batching-gpu-verify/aiverse_material_batches`; **set `AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches`** for this isolated Stage 82 build (correct invocation below).

```bash
AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches \
  bash tools/batching-stage80-ab.sh 1024 build/batching-stage82/stage83-clean
```

Require 6/6 exits, 38 checks each, real-MDI raster parity, and actual submissions for forced modes. If clean-run AUTO remains slower than forced MDI despite choosing MDI on 80+ frames, compare per-pass and whole-render cost, GPU queue inflight stalls and sampled cadence. Do not conflate CPU wall with GPU time. If clean AUTO is competitive, proceed to 10k and GPU timestamp instrumentation, keeping experimental flags OFF by default and PR #39 Draft.

**Actual work performed for Stage 83:** analyzed owner-provided build and trace log; GitHub handoff updated. No assistant-run native GPU/CTest, no CI or Vulkan validation.


---

## Stage 84 — clean crossover outcome and buffered per-frame CPU timeline (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, PR #39 (Draft). **Source change:** `adfe9a34d37bb5816f91a85d33e7ac790bd3f6d0` in `tests/render/material_batches.cpp`. Documentation appended as distinct commit. Owner's primary conflicted `AIVERSE` checkout stays untouched.

### Owner Mac Studio native Stage 83 clean six-run crossover

Executed in detached `AIVERSE-batching-stage82` using the prebuilt Stage 82 fixture **without** `AIVERSE_BATCH_AUTO_COST_TRACE`. Runner completed all six runs, every run printed `material palette CPU checks=38 result=passed`, screenshot equality `changed_channels=0`, and zero pipeline failures. Forced CPU MDI runs also printed `actual MDI pixel parity frame=100 changed_channels=0`. CPU synchronous `renderer.render()` medians (ms):

| Mode | Cycle 1 | Cycle 2 |
|---|---:|---:|
| Direct | 1.1802 | 1.1677 |
| CPU MDI | 0.8303 | 0.8201 |
| AUTO | 0.8488 | 1.4638 |

Cycle 1 AUTO (0.8488ms) came close to forced MDI (0.8303ms): approximately 28.1% faster than standalone Direct and ~2.2% slower than forced MDI. In Cycle 2 AUTO was substantially slower at 1.4638ms even though it **still used MDI** in 79/81 measured frames (755 MDI submissions), versus 80/81 and 800 submissions in Cycle 1. Forced MDI was consistently fast across runs, so the performance regression is not explained by simply choosing Direct. During Cycle 2 AUTO, shadow stage on final frame had 0.88979ms total / 0.54708ms backend submit, while opaque remained 0.34933ms total / 0.26158ms backend; both phases' cost varied, with unexplained large spikes. CPU time is not GPU time/FPS; thermal contention, GPU/CPU waits or interactive scheduling remain hypotheses, not confirmed causes. Do not turn AUTO on by default based on these six process runs.

### Stage 84 code change: opt-in deferred CSV

`AIVERSE_BATCH_FRAME_TIMELINE=1` in the `auto_stress` fixture records an in-memory sample **after** every `renderer.render()` call on batched frames 90..195, and writes the entire CSV only at frame 195. This avoids per-frame file I/O and `fprintf` logging overhead in the measured hot path. Output file: `<prefix>-frames.csv` with columns:

```text
frame,render_cpu_ms,mdi_submissions,mdi_commands,shadow_draws,opaque_draws,shadow_cpu_ms,opaque_cpu_ms,shadow_backend_ms,opaque_backend_ms
```

Model phase CPU and backend stage timings come from the existing `ModelRenderStats::mdiCpuStages` measured within the same frame; draw counts are renderer profile shadow/opaque. The timer brackets synchronous `renderer.render()` only and precedes timeline vector append. Writes/flushes at frame 195 are not included in the CPU sampled render time. A failed CSV file write marks the fixture failed. **This is not a GPU timestamp implementation**, and data only covers the scene's second phase, not warm-up Direct frames before frame 90. The CSV may expose successive frame spikes and whether they affect both passes or one pass, but cannot directly identify OS scheduling or Metal queue waits.

### Validation truth

Stage 83 six-run test evidence was provided by the owner and is described above. Stage 84 CSV source has been committed via GitHub, but **was not built or run on Mac Studio yet**; do not claim native CSV output, parity, performance or CI. No rendering/RHI/auto-policy production logic changed in Stage 84; production flags remain OFF by default.

### Exact next Mac command, safe detached worktree

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$WT" status --short
git -C "$WT" switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake --build "$WT/build/batching-stage82" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_AUTO=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
build/batching-stage82/aiverse_material_batches \
  unused.obj build/batching-stage82/captures/stage84-auto-1k \
  2>&1 | tee build/batching-stage82/stage84-auto.log
echo "Stage 84 exit=$?"
```

Require `material palette CPU checks=38 result=passed`, actual MDI pixel parity `changed_channels=0`, final `changed_channels=0`, `stress frame timeline samples=106`, and exit=0. The CSV path is `build/batching-stage82/captures/stage84-auto-1k-frames.csv`. Inspect spikes/contiguous windows with the same MDI mode. If no spikes appear in one run, repeat the isolated AUTO process 3+ times with unique prefixes to characterize variability without I/O trace. Next agent should use the actual CSV to decide whether the problem is in selection CPU time, backend submit, or external load. Keep PR #39 Draft and original main worktree untouched.


---

## Stage 85 — actual Stage 84 CSV analysis: steady-state AUTO MDI is stable in this run (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Owner evidence:** user-uploaded `stage84-auto-1k-frames.csv`, 106 rows with integer frames 90..195 and columns `frame,render_cpu_ms,mdi_submissions,mdi_commands,shadow_draws,opaque_draws,shadow_cpu_ms,opaque_cpu_ms,shadow_backend_ms,opaque_backend_ms`. Analyzed the actual file using Python pandas, not inferred from prior logs. This stage documents results only; no production source changes or Mac tests run by assistant.

### Quantified observations

- **MDI mode**: 93/106 rows have nonzero `mdi_submissions`, median synchronous `renderer.render()` **0.850792ms**, mean 1.005803ms inflated by early warmup outliers, min 0.751875ms, max 8.87179ms.
- **Direct mode**: 13/106 rows, median **1.190000ms**, mean 1.182105ms, min 1.113380ms, max 1.24629ms.
- Full MDI mode consistently submits 10 CPU indirect groups with 2048 commands and 5 shadow + 5 opaque draw calls. Direct mode has 0 MDI submissions and 1024 + 1024 draw calls. Those are distinct paths, so no claim that MDI is always faster in a real scene or on GPU.
- First cold/slow MDI frames:
  - Frame 91: render 8.871790ms, shadowCPU 4.036960ms, opaqueCPU 4.648370ms, backend submissions 2.762210ms + 1.500000ms.
  - Frame 93: render 3.901250ms, shadowCPU 2.001460ms, opaqueCPU 1.742420ms, backend submissions 1.655630ms + 1.556080ms.
  - Frame 95: render 3.563420ms, shadowCPU 1.778540ms, opaqueCPU 1.618130ms, backend submissions 1.471290ms + 1.442540ms.
  - Frame 97: MDI returns to 0.898250ms; frame 99: 0.899417ms.
- **After frame 97** (frames 98..195), no MDI-submitted row exceeds 1.2ms CPU render duration. MDI medians by further window: frames 100..115 ~0.842ms, 116..139 ~0.848ms, 140..159 ~0.844ms, 160..179 ~0.852ms, 180..195 ~0.859ms. This *one* recorded process has no sustained late-run MDI slowdown; the high Cycle 2 AUTO medians in the preceding six-run clean benchmark are not reproduced here. One healthy run cannot disprove intermittent slowdowns on another process run.
- Medians are unpaired samples from one sequential AUTO workload, not independent randomized A/B/A, and **all values are CPU synchronous wall time only**. GPU timestamps, completion latency, FPS, CPU frequency, scheduling and external contention are unmeasured. The earlier Mac GPU raster parity of this fixture was confirmed independently, but a CSV cannot itself verify pixel parity.

### Interpretation and next work

The immediate high-confidence conclusion is that the cold first 3 MDI samples heavily increase backend CPU submission cost; a warmed run can be stable around 0.85ms while Direct is around 1.19ms in this fixed scene. The prior apparently slow second AUTO process might reflect GPU queue stalls/CPU scheduler/thermal contention, but there is **no evidence yet** to attribute it to any single mechanism.

**Next tasks:** collect three independent Stage 84 CSV files using distinct `<prefix>` values in the same isolated worktree, preferably logging hardware/thermal state separately, and compare timelines by frame, especially whether spikes occur in `shadow_backend_ms`, `opaque_backend_ms` or the remainder. Collect GPU command-buffer or timestamp timing separately. Before changing the policy or declaring performance ready, repeat forced MDI alongside AUTO under different process order and capture actual GPU parity and no-submission guards. Keep experimental flags OFF by default and Draft PR #39 open. Handoff must report any new code commits and Mac test outcomes.

**Verification truth:** Python-loaded uploaded CSV successfully parsed, shape (106,10). This is analysis/handoff only; no native compilation, CTest, CI or GPU run was performed in this stage.


---

## Stage 86 — independent AUTO frame-time replication with forced CPU MDI controls (2026-11-19)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/auto-batch-cost-metrics-20261119`; **PR:** #39 Draft. **New files:** `tools/batching-stage86-trials.sh` and `tools/batching-stage86-analyze.py`. **Commits:** `af30bc0cfe32283b5ab3d0bb19fef60260564084` (runner), `c340235033f437602bd7fa2861818fceed4cf4d6` (analyzer), `abb0bc55fc85e3651323fce5d1e36400a0fcd315` (shell output-variable initialization fix). This handoff is a separate subsequent commit. **No runtime renderer/RHI/policy change.** Existing flags OFF by default.

### Prior measurements motivating this work

Stage 83 clean Mac Studio rotated 1k-primitive benchmark showed:
- Direct `renderer.render()` CPU median: 1.1802 / 1.1677 ms.
- Forced CPU MDI median: 0.8303 / 0.8201 ms, real MDI screenshot byte equality in both trials.
- AUTO median: 0.8488 / 1.4638 ms; while AUTO selected MDI for 80/81 and 79/81 measured frames in successive runs. Performance is therefore variable and cannot be explained by frequent Direct fallback.
- A subsequent owner-provided Stage 84 CSV, 106 frames (90..195), had 93 MDI frames and MDI median ~0.850792ms; after initial cold MDI frames 91,93,95, no successful-MDI frame after 97 exceeded 1.2ms. This single healthy run does not explain why another process had sustained late high times.

### New benchmark implementation

- `bash tools/batching-stage86-trials.sh [ITEMS=1024] [OUTPUT=build/batching-stage82/stage86-trials]` runs **five separate processes** in order `AUTO1, forced-MDI1, AUTO2, forced-MDI2, AUTO3`. Each has isolated process-local shader/Metal caches, baseline warmup and capture prefix. This is not randomized per-frame A/B/A, but interleaving provides a contemporary control for environmental performance drift.
- Every mode sets `AIVERSE_PALETTE_TEST_CASE=auto_stress`, `AIVERSE_BATCH_CPU_TIMING=1`, `AIVERSE_BATCH_FRAME_TIMELINE=1`, `AIVERSE_CAPTURE_SUCCESSFUL_MDI=1` and its explicit policy. Inherited GPU ICB, AUTO trace, GPU authored path and other conflicting benchmark flags are removed from the child environment. AUTO alone enables `AIVERSE_BATCH_AUTO=1`; forced-MDI alone enables `AIVERSE_METAL_CPU_MDI=1`.
- The runner requires successful executable exit via `set -euo pipefail`, 38-or-current-count CPU `result=passed`, an actual-MDI pixel-equality message, final byte equality, 106 CSV rows logged, CSV file existence, and real forced-MDI samples. Log and CSV files are unique per trial.
- New Python 3 standard-library analyzer reads the five CSVs, strictly validates header, numeric fields and frames 90..195, and writes `summary.csv` with measured-frame CPU median, P95, last-36-frame median, MDI/Direct frame counts, actual submissions, warmed MDI frames exceeding 1.2ms, longest consecutive slow run, per-pass CPU-stage and backend-submit median, and the worst frame's stage costs. Also prints a compact run comparison. The 1.2ms threshold is a diagnostic reference from prior 1k results, **not** a production latency requirement or an adaptive decision threshold.
- Bash issue found during static review: `local label=... prefix=... ` expanded prefix before label assignment with `set -u`. It was corrected in `abb0bc55`. No native execution reported.

### Validation status

**Implemented and committed** via GitHub connector. Inspected the live runner/CSV output contract. **NOT RUN** after these new Stage 86 changes: Mac shell, Python analyzer against generated five-run files, CMake/CTest, Metal raster, Vulkan or GPU timers. Prior Stage 83/84 owner results remain the only actual native evidence. No claim of 3 independent runs or stability verification should be made until owner logs arrive.

### Exact next owner Mac Studio command (isolated worktree)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$WT" status --short
git -C "$WT" switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1

# Only the Python/shell scripts changed since Stage 84; reuse Stage 82 binary.
cd "$WT" || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches \
  bash tools/batching-stage86-trials.sh \
  1024 build/batching-stage82/stage86-trials
```

The scripts print `Stage 86 PASS` only after all five child executables and CSV summary succeed. Share `build/batching-stage82/stage86-trials/summary.csv` and optionally the three AUTO CSVs if there are slow frames. Review outlier provenance against interleaved forced-MDI CPU controls before retuning the CPU AUTO policy or touching Metal. Keep Draft PR #39 until native verification. The main user's conflicted worktree remains untouched.

### Following tasks

If all three AUTO runs are stable near forced MDI and only first-cold MDI frames spike, quantify cold-start versus settled performance and consider GPU-timestamp instrumentation next; do not unconditionally enable AUTO. If AUTO sometimes runs slowly but forced MDI controls do not, use the per-pass backend timings in CSVs to isolate which phase changes. If both are slow together, prioritize system/device contention and native GPU completion instrumentation over policy changes. Verify shader/geometry permutations, Vulkan cross-platform parity, and full CI before release.


---

## Stage 87 — five-process Stage 86 shows backend/environment variability, not only AUTO (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Owner-tested previous script:** `tools/batching-stage86-trials.sh` and `tools/batching-stage86-analyze.py` from Stage 86. **New analyzer change:** `7af48913c4dea101aee080282a6a5b4c71f069df` in `tools/batching-stage86-analyze.py`. Documentation in this follow-up commit. **Native execution of the new analyzer revision has NOT yet occurred.** No renderer/Metal RHI/CPU policy changes.

### Actual owner Mac Studio Stage 86 results

The owner ran all five processes and the runner printed `Stage 86` summary and `changed_channels=0` at the end. The complete printed summary:

```text
Trial    MDI/81    median       p95      late   MDI>1.2  streak  worst frame
auto1        76    0.8063    1.7736    0.7775        12       6          141
mdi1         81    1.5965    1.9701    1.6193        59       8          178
auto2        80    0.8393    1.8285    1.5731        26       5          162
mdi2         81    0.8620    0.9219    0.8726         0       0          159
auto3        80    1.3900    1.8787    1.4392        48       8          173
```

**Key finding:** forced MDI without AUTO changes CPU median from `1.5965ms` (MDI1) to `0.8620ms` (MDI2), a ~85% difference, and MDI1 has 59 sampled MDI frames above 1.2ms while MDI2 has 0. Thus the slowdown is **not only caused by AUTO mode selection**; CPU backend work, queue synchronization, scheduling/thermal and concurrent system load remain possible explanations. AUTO still uses MDI in 76–80 of 81 measured frames, confirming mode selection is not the main cause of this run's differences. The healthy low-latency runs indicate the engine is capable of a ~0.8–0.9ms CPU render on this fixed synthetic scene, but neither sustained GPU improvement nor general FPS gains have been proven.

All five runs returned to the runner and generated a summary; detailed CSVs have **not** yet been provided to this agent for Stage 86. Do not assert exact per-frame backend costs or identify an individual OS/GPU cause from only the printed summary. The final `changed_channels=0` appears in the owner log; the runner's own assertions require actual-MDI screenshot parity and CPU invariants in every mode before completing successfully.

### Stage 87 analyzer implementation

Without changing the original CSV input format, the analyzer now:
- Identifies contiguous **measured-frame** slow intervals above the previous diagnostic 1.2ms CPU threshold, including first/last frame indices of the longest slow streak.
- Separates each slow interval's **median CPU time in two Metal submit sub-stages** (`shadow_backend_ms + opaque_backend_ms`) from the rest of synchronous render CPU time (`render_cpu_ms - backend_sum`). The remainder includes all other CPU work; it **must not be labelled a GPU wait**.
- Reports summed median backend costs for slow MDI frames, longest slow interval and worst frame to `summary.csv`, and prints concise per-run attribution lines.
- Preserves existing per-run medians, P95, MDI submission counts, raster parity gates, run order and all data sources. No GPU timers and no production flags changed.

### Validation performed and next precise instructions

**Performed:** analyzed actual owner-reported five-run summary and modified GitHub analyzer source. **Not performed:** running changed Python analyzer, verifying all per-frame values from owner CSVs, native CMake, CTest, new Metal tests, GPU timestamps, Vulkan and CI. PR remains Draft.

Re-run the analyzer only against the already generated five CSV files — no GPU re-render or rebuild needed:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1
python3 tools/batching-stage86-analyze.py \
  build/batching-stage82/stage86-trials 1024
```

The output is `build/batching-stage82/stage86-trials/summary.csv`; share that CSV plus any slow-run `auto3-frames.csv`, `mdi1-frames.csv`, `auto2-frames.csv` to diagnose the stages. A direct read of the raw CSV is needed before concluding whether backend encoding, render setup or process scheduling dominates. Keep primary AIVERSE conflict worktree untouched.

**Next engineering action:** if the slow intervals clearly correlate with Metal backend submit CPU stages, add independent opt-in timing around the Metal CPU ICB encoding loop/argument-buffer stage and/or GPU command buffer waits; if they correlate with other CPU work, inspect command submission cadence and renderer preprocessing. Run more clean A/B rotated trials only after locating the dominant cost. Do not enable AUTO by default or merge PR #39 without native checks.


---

## Stage 88 — separate slow Direct vs MDI streaks and use row-paired CPU attribution (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Change:** `75500fbf58ff1fde862d77af6863d857ceb09d79` in `tools/batching-stage86-analyze.py`. This documentation is a subsequent commit. **No renderer/Metal/CPU policy changes**; all production paths/flags unchanged.

### Actual owner-provided Stage 87 output (updated Stage 86 CSV analyzer)

The owner successfully executed the Stage 87 analyzer in their Mac Studio worktree and pasted:

```text
Trial    MDI/81    median       p95      late   MDI>1.2  streak  worst frame
auto1        76    0.8063    1.7736    0.7775        12       6          141
mdi1         81    1.5965    1.9701    1.6193        59       8          178
auto2        80    0.8393    1.8285    1.5731        26       5          162
mdi2         81    0.8620    0.9219    0.8726         0       0          159
auto3        80    1.3900    1.8787    1.4392        48       8          173

Slow intervals (>1.2ms CPU render; diagnostic threshold, not SLA):
auto1: longest=134..139, backend_median=0.0000ms non_backend_cpu_median=1.2608ms, worst_backend=0.4898ms worst_non_backend_cpu=1.4717ms
mdi1: longest=165..172, backend_median=1.0396ms non_backend_cpu_median=0.6619ms, worst_backend=1.2015ms worst_non_backend_cpu=0.8278ms
auto2: longest=173..177, backend_median=0.8687ms non_backend_cpu_median=0.7193ms, worst_backend=0.6110ms worst_non_backend_cpu=1.7796ms
mdi2: longest=-1..-1, backend_median=nanms non_backend_cpu_median=nanms, worst_backend=0.6148ms worst_non_backend_cpu=0.3290ms
auto3: longest=148..155, backend_median=0.8040ms non_backend_cpu_median=0.8042ms, worst_backend=0.4861ms worst_non_backend_cpu=1.7400ms
```

**Conservative interpretation:** in the forced MDI1 slow interval, ~1.04ms is attributable to two CPU backend submission sub-stages and ~0.66ms to other synchronous CPU work. In AUTO2 and AUTO3 both backend and other CPU work are material. AUTO1's longest slow interval has zero median measured backend CPU submit time: likely the policy chose Direct on many or all those frames, but the previous analyzer did **not** label frame mode in contiguous intervals, so that remains unverified. The worst frame of a run is also not necessarily within its longest interval. Neither the zero backend metric nor the remainder proves GPU idling or waiting. Only the printed derived summary was inspected; raw five CSVs were not available in this agent environment. All values are CPU wall, **not** GPU execution or FPS.

### Root cause of ambiguous diagnostic attribution

Previous `longest_slow_backend_median_ms` used the sum of two independently computed phase medians, which need not equal a median of per-frame total backend CPU time. It also combined contiguous slow Direct and MDI frames into one interval, making `backend=0` ambiguous. The classification needed to be improved before modifying renderer decisions.

### New code

- `mode_name(row)`: classify by `mdi_submissions>0` on **each actual completed frame**.
- `mode_slow_runs(rows, mode)`: return contiguous slow frames matching exactly that mode; a Direct frame breaks an MDI slow run, and vice versa. The existing overall slow interval and summary columns remain.
- `median_cost(group)`: computes backend = shadow + opaque on each *same* frame, then takes the median of those row-level sums, plus a separate median of remaining synchronous CPU time. The costs are explicitly **not** GPU stalls and the median of backend plus the median of other need not equal the total frame median.
- Extended `summary.csv` fields: mode composition of the longest overall interval; longest MDI-only and Direct-only slow intervals with start/end/count and CPU backend/other medians; paired backend median for all MDI frames and for slow MDI frames.
- Prints a new section `Mode-separated slow intervals (MDI and Direct never share a streak):` after the original diagnostic report.
- No new benchmark run is required; reuse existing `auto1`, `mdi1`, `auto2`, `mdi2`, `auto3` CSV files.

### Validation truth and exact next Mac command

**Performed:** inspected output pasted by owner, used GitHub connector to read and update analyzer. **NOT PERFORMED:** execution of Stage 88 analyzer on actual 5 CSVs, Python unit tests, native CMake/CTest, Vulkan, GPU timestamp probes. Keep PR #39 Draft.

Avoid changing the checkout because its worktree can have an in-progress rebase: fetch and extract the single Python file from the remote Git object to `/tmp`, preserving all worktrees:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage86-analyze.py \
  > /tmp/aiverse-stage88-analyze.py || exit 1
python3 /tmp/aiverse-stage88-analyze.py build/batching-stage82/stage86-trials 1024
```

Share `Mode-separated slow intervals` and/or regenerated `summary.csv`; this is the next evidence needed to decide whether to instrument Metal ICB CPU encode vs rendering preparation vs OS scheduling. Once mode-specific CPU costs are established, test any hypothesis in a targeted A/B, do not blindly change AUTO thresholds. Maintain per-stage handoff and preserve default-off feature flags.


---

## Stage 89 — owner validates mode-separated slow CPU episodes; opt-in Metal ICB slow-stage trace (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, **PR #39 Draft**.
**Production code touched:** `engine/gfx/metal/GfxMetal.mm`, commit `a3347861a3c75dccf677c1f1bbdcf890cb0fc4ea`.
**Agent documentation:** this section is in a separate subsequent commit.
**Testing status:** owner's Stage 88 Python analysis ran successfully using the existing five Stage 86 CSVs. **New Metal C++/ObjC++ slow trace has not yet been compiled or executed on Mac Studio**. No GPU timestamps, Vulkan, full CTest, or CI results established for this stage.

### Verified owner evidence (Stage 88 reanalysis)

Five original CPU render() medians/p95, all ms:
```text
auto1  0.8063 / 1.7736   MDI 76/81
mdi1   1.5965 / 1.9701   MDI 81/81
auto2  0.8393 / 1.8285   MDI 80/81
mdi2   0.8620 / 0.9219   MDI 81/81
auto3  1.3900 / 1.8787   MDI 80/81
```

Actual `Mode-separated slow intervals` output from Mac:
```text
auto1: MDI 127..129 (3 frames), backend=0.9812ms, other_cpu=0.7001ms; Direct 136..138 (3 frames), other_cpu=1.2872ms
mdi1:  MDI 165..172 (8 frames), backend=1.0469ms, other_cpu=0.6619ms; Direct -1..-1 (0 frames)
auto2: MDI 173..177 (5 frames), backend=0.8694ms, other_cpu=0.7193ms; Direct 153..153 (1 frame), other_cpu=1.2445ms
mdi2:  MDI -1..-1 (0 frames); Direct -1..-1 (0 frames)
auto3: MDI 175..180 (6 frames), backend=0.9579ms, other_cpu=0.6678ms; Direct 153..153 (1 frame), other_cpu=1.2449ms
```

Interpretation: slow MDI-only episodes occur in both forced-MDI (mdi1) and AUTO. Two CPU backend submission sub-stage totals are significant (~0.87..1.05 ms median during longest slow MDI episodes), and other synchronous CPU work is significant (~0.66..0.72 ms). AUTO1 additionally has slow Direct-only frames. Therefore **not a pure policy-selection bug**; do not change AUTO EWMA or threshold on this evidence. CPU wall-time, not actual GPU duration/FPS; no OS scheduling or GPU-stall cause yet established. The analyzer's per-frame paired backend/other medians do not add linearly to the total CPU frame median.

### Code implementation

Previously `AIVERSE_ICB_PROFILE=1` reported averages across a large submission window, hiding individual slow submission stage breakdown. Added an **explicit opt-in** `AIVERSE_ICB_SLOW_TRACE=1` within the existing CPU Metal `drawIndexedIndirect` backend. After actual `executeCommandsInBuffer` submission, compute the same four measured CPU intervals already used by the aggregate trace:
- `args`: validation/pool/PBR argument setup up to `afterArgs`
- `alloc`: ICB reset + uniform snapshots through `afterAlloc`
- `encode`: C++/Objective-C per-command ICB construction through `afterEncode`
- `residency_execute`: resource declarations plus CPU-side `executeCommandsInBuffer` encoding, through `afterExecute`

If total exceeds configurable `AIVERSE_ICB_SLOW_TRACE_MS` (default `0.15` ms, valid 0.01..100), print `[metal-icb-slow] frame=... slot=... commands=... total=... args=... alloc=... encode=... residency_execute=... threshold=... sample=.../24`. Log **at most 24 events per process**. No clock in the inner draw loop; the no-flag path has only a constant conditional and no per-command cost. This diagnostic does not change ICB buffer allocation, pipeline, encoder bindings, residency, synchronization, fallback, selection or GPU authoring. Logs can still perturb the immediately following frames; **do not use traced run as throughput proof**.

### Next Mac Studio build and targeted validation

Use the existing standalone worktree, but note it has previously been observed with an in-progress rebase. **Do not call `git switch` / `git reset --hard` / `rebase --abort` there.** Create a fresh **detached** dedicated worktree at the remote tip:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage89
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage89" -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage89" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
mkdir -p build/batching-stage89/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_MS=0.15 \
build/batching-stage89/aiverse_material_batches unused.obj \
  build/batching-stage89/captures/stage89-mdi-1k \
  2>&1 | tee build/batching-stage89/stage89-mdi.log
echo "Stage 89 exit=$?"
```

A single forced MDI run is a focused probe, not a five-trial performance comparison. Require existing `material palette CPU checks=38 result=passed`, `actual MDI pixel parity ... changed_channels=0`, final `changed_channels=0`, real `measured_mdi_frames=81`, expected CSV, and exit 0. `[metal-icb-slow]` lines may be absent if all submissions stay under 0.15ms. If absent and CPU slowdown does occur, lower the diagnostic threshold to 0.08ms and rerun only if appropriate. If slowdown is unobserved, do not claim backend fixed; compare against a healthy control run and repeat measurements. Capture log plus per-frame CSV.

### Following stage

Correlate specific expensive Metal ICB samples (frame, slot, draw count, four sub-stages) with frame CSV's `shadow_backend_ms` and `opaque_backend_ms`; identify whether slow time comes from actual encode loop, uniform/ICB allocations, or residency/command issuance. Distinguish slow *CPU* path from GPU runtime with independent native Metal command buffer / GPU timestamps when viable. Keep PR Draft and experiment flags OFF by default; no engine performance claim until repeatability.


---

## Stage 90 — Mac Metal CPU MDI cold alloc diagnosed; filtered warm slow-ICB trace (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Code change:** `f1dcfbb7079fb4a25b738c0bef0e7cca8f18ecc3` in `engine/gfx/metal/GfxMetal.mm`. This handoff is a separate commit. **Stage 90 code not yet compiled/run on Mac.** GPU/CPU experimentation remains explicitly opt-in, all defaults unchanged.

### Real owner-provided Stage 89 Metal evidence

Owner built the real Mac Objective-C++ renderer and ran forced `mdi` with `AIVERSE_ICB_SLOW_TRACE=1`, threshold 0.15ms. Actual output:
- `material palette CPU checks=38 result=passed`; actual MDI raster parity `frame=100 changed_channels=0`; final `changed_channels=0`; `Stage 89 exit=0`; 81/81 measured frames submitted real MDI (810 submissions).
- Forced-MDI `renderer.render()` sampled median 1.6058ms, P95 1.9307ms; late 36-frame median 1.6127ms. It was a **slow run**, matching previous intermittent slow forced MDI results.
- First 24 `[metal-icb-slow]` lines all came from **frames 91..93**, because the diagnostic printed only the first 24 submissions exceeding 0.15ms and the first CPU ICB builds were very expensive.
- Frame 91 slot 0 (227 commands): total 0.92938ms, **alloc 0.81671ms**, encode 0.07067ms, residency/execute 0.03979ms, args 0.00221ms. The cold `alloc` includes ICB allocation/reset, command-buffer and uniform-resource preparation; exact OS/driver cause is not established.
- Most other cold frame 91..93 227-command submissions: total ~0.31–0.36ms, `alloc` ~0.25–0.30ms, `encode` ~0.045–0.055ms. The selected subset therefore proves **cold ICB setup dominated initial slow submissions**. It does **NOT** explain late-frame slowdown: no trace lines for frames after 93 and late median was still 1.6127ms.
- Later final-frame renderer stage: shadow backend submit 0.24667ms and opaque backend submit 0.23925ms, indicating at least some warmed frame could be quick even in slow experiment; no raw per-frame CSV for Stage 89 was provided.

### Stage 90 implementation

Enhances the existing `AIVERSE_ICB_SLOW_TRACE=1` diagnostic with two **optional static parsed flags**:
- `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`: ignore CPU ICB submit events before frame 120; valid 0..1,000,000, default 0. Preserves 24-event budget for warmed frames and intermittent late slow periods.
- `AIVERSE_ICB_SLOW_TRACE_SLOT=0`: optional per-frame submission index filter (valid 0..64); default all slots. Choosing 0 lets us compare similarly sized 227-command groups once per MDI-submitting frame and reduces stdout/trace distortion. `slot` refers to the index of the command submission in the current Metal frame slot's pool; prior logs indicated 0..9 per frame.
- Existing `AIVERSE_ICB_SLOW_TRACE_MS` default 0.15ms and max 24 printed messages stay unchanged. No extra timing inside the per-draw loop, no resource, pipeline, command or GPU behavior modification. The filtering is diagnostic only.

### Actual status and next exact Mac Studio validation

**Verified:** owner Stage 89 native build/correctness and 24 cold trace samples. GitHub source updated for Stage 90. **Unverified:** new Stage 90 compilation, real late-frame filtered trace, distinct CPU vs GPU wait mechanism, GPU duration/timestamps, Vulkan/full CI. Do not conflate CPU wall with GPU runtime.

Because the owner's primary worktree and an older batching worktree have unresolved rebase conflicts, create a separate **new** worktree for Stage 90 (check its path first; do not overwrite existing work):

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage90

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage90" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage90" --target aiverse_material_batches --parallel 8 || exit 1

cd "$WT" || exit 1
mkdir -p build/batching-stage90/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 \
AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage90/aiverse_material_batches unused.obj \
  build/batching-stage90/captures/stage90-mdi-1k \
  2>&1 | tee build/batching-stage90/stage90-mdi.log
echo "Stage 90 exit=$?"
```

Require native build, 38 CPU checks, actual MDI `changed_channels=0`, final pixel parity, real 81/81 measured submissions, CSV and exit=0. Trace threshold `0.08` ms is intentionally below the 0.15ms initial cutoff to catch moderate slow slot-0 submissions; no log entries would mean that slot remained below threshold for all allowed frames, not that every Metal ICB submission was fast. The 24-line budget and optional slot filter intentionally sample, not exhaustively profile.

### Next

Analyze `[metal-icb-slow]` lines from frames >=120 together with Stage 90 `-frames.csv` for frame-level CPU backend/other costs. If warmed `alloc` dominates during slow MDI intervals, investigate pool growth, uniform allocation/reuse, `resetWithRange` cost or driver scheduling without removing explicit bindings. If `encode` dominates, investigate CPU command setup overhead and validated alternatives. If measured backend is fast but overall CPU slow, profile renderer setup/visibility/grouping and OS scheduler. Only make changes with clear evidence, confirm pixel parity and repeated low-variance performance afterward. Keep PR #39 Draft/default opt-ins OFF.


---

## Stage 91 — warm Metal ICB encode dominates; isolate reset vs uniforms (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39.
**Source commit:** `de9892f6ac6bb33ea1b37f10427697d8e0da4b1f` in `engine/gfx/metal/GfxMetal.mm`. Handoff recorded in a separate commit.
**Correctness:** No change to Metal commands, pipeline, buffer reuse, synchronization, engine defaults or the AUTO selector. Added only one clock reading per *submitted CPU ICB* and two diagnostic values on a slow-trace opt-in. Stage 91 code has NOT been compiled or executed on Mac.

### Owner's actual Stage 90 Mac Studio evidence

The owner successfully compiled the prior Stage 90 code and executed real Metal forced CPU MDI for 1,024 unique primitives with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`, `_SLOT=0`, `_MS=0.08`. All 38 CPU checks PASS, real MDI pixel parity on frame 100 `changed_channels=0`, final capture `changed_channels=0`, exit 0, 81/81 measured forced MDI frames and 810 actual MDI submissions. Full batched phase `renderer.render()` median **1.4087ms**, P95 **1.9296ms**, late-frame median **1.5407ms**. This is another slow forced-MDI run, showing that mode selection is not the root cause.

24 filtered warm `slot=0`, `commands=227` samples from frames 120..162 had `encode` generally ~0.085..0.113ms, a high sample 0.13658ms on frame 140, `alloc` generally 0.009..0.016ms, `residency_execute` ~0.010..0.027ms, and negligible `args`. E.g. frame 120 total 0.12400ms, encode 0.09287ms, alloc 0.01333ms, residency-execute 0.01771ms, args 0.00008ms. This *selected submission* has ~75% encode CPU cost. The first cold frames from Stage 89 had been alloc/reset-dominated. **Do not attribute the entire ~1.5ms per-frame CPU render duration solely to ICB encoding:** a frame has ten submissions and model prep, and the trace sampled only slot 0. CPU time is not GPU completion/FPS.

### Stage 91 diagnostic refinement

The existing Metal CPU ICB `alloc` metric spans:
(a) creation/resize/reset of `MTLIndirectCommandBuffer` and
(b) private-to-submission uniform-buffer allocations, copy and active binding-table preparation. Those are distinct sources of cost. Stage 91 inserts a timestamp `afterIcbReset` immediately after `[icb resetWithRange:...]` and **only in the opt-in diagnostic output** splits the existing `alloc` interval into `icb_reset` and `uniforms`, both ms. The output remains capped at 24 slow submissions, and the preceding aggregate `AIVERSE_ICB_PROFILE` remains unchanged. The exact same ICB resource binding and lifetime logic remains. This helps determine whether allocation/reset or CPU uniforms work dominates when reproducing slow frames. There is still no per-frame cross-slot aggregate and no GPU timestamps; those require separate work.

### Next owner Mac run

Prefer a new isolated worktree because previous project worktrees had unresolved rebase state. Do not remove or reset them. Clone just a detached worktree from the new branch tip, then compile a focused fixture:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage91
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage91" -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage91" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
mkdir -p build/batching-stage91/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 \
AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage91/aiverse_material_batches unused.obj \
  build/batching-stage91/captures/stage91-mdi-1k 2>&1 | tee build/batching-stage91/stage91-mdi.log
echo "Stage 91 exit=$?"
```

Pass criteria: native build succeeds; `material palette CPU checks=38 result=passed`; `actual MDI pixel parity ... changed_channels=0`; final `changed_channels=0`; 81 MDI measured frames with nonzero commands, no backend failure, and exit 0. Inspect `[metal-icb-slow]` lines for **`icb_reset` vs `uniforms`**, alongside `encode`; collect per-frame CSV to correlate late CPU times. Slow-trace output is sampled and potentially perturbs later frames: performance decisions require separate untraced repeated controls. Preserve Draft PR #39 until native verification and external cross-platform tests.


---

## Stage 92 — owner Mac fast forced-MDI run passes; intermittent slowdown not reproduced (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Tested code:** Stage 91 `de9892f6ac6bb33ea1b37f10427697d8e0da4b1f`, handoff `77ab4bb94cf848f8cd3327c22f8a60e2e38a5c2a`. This Stage 92 update documents owner-provided **native Mac Studio evidence**; no renderer/RHI/policy change or assistant-run native test.

### Actual results from owner Mac Studio

```text
[100%] Built target aiverse_material_batches
material palette CPU checks=38 result=passed
stress requested_items=1024 authored_unique_primitives=1024 submitted_model_draws=1
batch=0 shadow=1024 opaque=1024 total=2050 error=
actual MDI pixel parity frame=100 changed_channels=0
CPU render benchmark direct: samples=66 median_ms=1.2233 p95_ms=2.3969
CPU render benchmark batched: samples=81 median_ms=0.7739 p95_ms=0.8303 min_ms=0.6899 max_ms=0.9340
stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=810
CPU render benchmark auto_mdi_submitted_frames: samples=96 median_ms=0.7742 p95_ms=0.8416
CPU render benchmark auto_late_frames: samples=36 median_ms=0.7860 p95_ms=0.8549
successful MDI CPU stage pass=0 samples=96 submissions=480 mean_total_ms=0.34075 grouping_ms=0.01701 submit_ms=0.23464
successful MDI CPU stage pass=1 samples=96 submissions=480 mean_total_ms=0.30895 grouping_ms=0.00729 submit_ms=0.24805
stress frame timeline samples=106 path=build/batching-stage91/captures/stage91-mdi-1k-frames.csv
batch=1 shadow=5 opaque=5 total=12 error=
stress phase=auto model_draw_calls=10 cpu_mdi_submissions=10 cpu_mdi_commands=2048 gpu_icb_submissions=0 gpu_icb_commands=0
changed_channels=0
Stage 91 exit=0
```

The test used `AIVERSE_ICB_SLOW_TRACE=1`, `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`, `AIVERSE_ICB_SLOW_TRACE_SLOT=0`, `AIVERSE_ICB_SLOW_TRACE_MS=0.08`. **No `[metal-icb-slow]` output** appeared, meaning no eligible sampled slot-0 submission crossed the threshold in this process (subject to trace instrumentation/sampling), NOT that other slots or the GPU were fast. The dedicated `icb_reset` vs `uniforms` diagnostic therefore did not produce observations in this run; its compilation and raster behavior still passed.

### Interpretation and next steps

The forced CPU MDI median 0.7739ms vs sequential Direct baseline 1.2233ms implies an observed ~36.7% lower CPU `renderer.render()` median for this specific synthetic 1k-primitive scene. The late 36-frame MDI median 0.7860ms and P95 0.8549ms do not reproduce the intermittent late slowdown in Stage 89/90, where forced MDI medians were 1.6058/1.4087ms respectively. No code optimization took place between those runs, only opt-in diagnostics differed; do not credit the Stage 91 diagnostic timestamp with a speedup. The variation may be influenced by Metal driver/CPU scheduling/contention but no root cause has been proven. GPU timing/FPS and cross-platform validation are still absent.

**Next experiment without changing renderer:** repeat the *same* compiled Stage 91 forced-MDI process at least three times with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=160`, no SLOT filter, `AIVERSE_ICB_SLOW_TRACE_MS=0.08`, each with a unique output capture prefix and `AIVERSE_BATCH_FRAME_TIMELINE=1`. A trace capturing all late submission slots will reveal whether a slow recurrence arises in slot0 or another slot, and the generated per-frame CSV can establish whether slow `renderer.render()` coincides with CPU submission stages. Compare against an untraced control to exclude logging effects; preserve submitted count and screenshot parity. Do not rename/rewrite any existing worktree or remove an unfinished rebase.

If repeated runs remain consistently fast, defer further micro-optimizations and proceed to representative 2k/4k load, then controlled 10k with GPU timestamps/memory metrics. Keep CPU MDI/AUTO default-off and PR #39 Draft until validated.


---

## Stage 93 — owner three late forced-MDI runs: two slow, one fast; stage timing evidence (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Tested code:** Stage 91 through `77ab4bb94cf848f8cd3327c22f8a60e2e38a5c2a`. Owner ran three forced CPU-MDI processes with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=160`, all slots and 0.08ms threshold. **This stage is evidence/handoff only; no production or diagnostic C++ changes.**

### Actual native Mac Studio evidence

All three forced MDI runs executed `aiverse_material_batches` and produced `material palette CPU checks=38 result=passed`, real MDI raster `actual MDI pixel parity frame=100 changed_channels=0`, final image parity `changed_channels=0`, 81/81 measured frames submitted 810 CPU MDI groups, 0 GPU ICB, and saved 106-row per-frame CSVs:
- `build/batching-stage91/late-probes/mdi-1-frames.csv`
- `build/batching-stage91/late-probes/mdi-2-frames.csv`
- `build/batching-stage91/late-probes/mdi-3-frames.csv`

CPU `renderer.render()` medians / P95 / late-36-frame medians (ms):

| Trial | Median | P95 | Late median |
|---|---:|---:|---:|
| Forced MDI1 | 1.3936 | 1.8735 | 1.3254 |
| Forced MDI2 | 1.3699 | 1.9353 | 1.2674 |
| Forced MDI3 | 0.8277 | 0.9210 | 0.8297 |

The 24-line `[metal-icb-slow]` output limit was consumed on frames 160..164 for slow MDI1/MDI2, so later intervals **were not observed** by the detailed trace, despite the slow median being sustained. Fast MDI3 reported only three >0.08ms slow samples from frames 190, 193 and 196 (frame 196 is beyond the CSV capture window ending frame 195, so do not attempt to correlate it with the sampled 81-frame benchmark).

For 227-command warm samples from slow MDI1 and MDI2, `encode` frequently ~0.08–0.10ms, with occasional ~0.13–0.15ms. `icb_reset` normally ~0.005–0.008ms and `uniforms` ~0.003–0.005ms; `residency_execute` usually ~0.005–0.02ms. This indicates **sampled warm ICB calls are primarily CPU encoding rather than allocations**, contrary to initial frames 91..93, where cold allocations dominate. It does **not** prove whether all per-frame slowdowns come from encoding because nine or ten submissions and renderer CPU preparation add up, and the trace contains only the first 24 events.

### Interpretation

The intermittent slowdown occurs in forced CPU MDI without AUTO, so further adjusting the AUTO selector is unjustified. There are at least two CPU cost profiles: fast process `renderer.render()` median ~0.83ms and slow processes ~1.37–1.39ms. Causes such as process scheduling, GPU queue waits/CPU state, GPU and CPU thermal/frequency conditions or driver overhead remain **hypotheses**; no GPU timing, system utilization or OS waiting instrumentation collected. Both slow and fast processes pass pixel parity on this fixed 1,024-unique-primitive scene.

### Next exact diagnostic procedure — no source change required

Existing `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME` flag already supports beginning output after frame 175; it does not require another source commit. Use the existing Stage 91 executable and a fresh prefix; either trace just `slot=0` over 175..195 or all slots (24 messages cap can still fill in ~3 frames). Prefer slot 0 to compare same group across frames, but if full backend attribution is required use the existing per-frame CSV that accumulates both passes without the 24-line cap.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage91 || exit 1
mkdir -p build/batching-stage91/late175
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=175 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage91/aiverse_material_batches unused.obj \
  build/batching-stage91/late175/mdi-175 \
  2>&1 | tee build/batching-stage91/late175/mdi-175.log
```

Require `checks=38 passed`, actual MDI pixel parity 0, final pixel parity 0, real MDI frame count 81/81 and process exit 0. For a more informative answer with **no further GPU runs**, request the three already created `mdi-1-frames.csv`, `mdi-2-frames.csv`, `mdi-3-frames.csv` for the CPU stage time series. In particular, compare same frame ranges and whether both shadow and opaque backend submit costs increase together. Do not equate backend CPU wall with GPU execution. PR #39 stays Draft; experimental flags remain OFF by default.

**Validation truth:** owner-provided Mac native logs were analyzed; GitHub handoff updated. Assistant did not compile/run Mac renderer or perform GPU timing.


---

## Stage 94 — direct analysis of three uploaded 106-frame forced-MDI timelines (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Source evidence:** user uploaded `mdi-1-frames.csv`, `mdi-2-frames.csv`, `mdi-3-frames.csv` from `build/batching-stage91/late-probes/`, each 106 rows covering frames 90..195. All three parsed successfully in Python/pandas in the assistant runtime and have the ten expected columns (frame, renderer CPU, MDI submissions/commands, shadow/opaque draws, shadow/opaque CPU pass time, shadow/opaque backend CPU submission). This stage is **data analysis and documentation only**. No renderer/RHI, test harness or policy code changed.

### Owner native correctness results already established

All three underlying Mac Studio Stage 92 forced-MDI processes printed `material palette CPU checks=38 result=passed`, `actual MDI pixel parity frame=100 changed_channels=0`, `changed_channels=0` for final capture, and recorded 81 of 81 measured frames with 810 submitted CPU MDI groups. Across every CSV frame in the measured interval 115..195, the submission counts are consistently **10 CPU-MDI batches, 2048 indirect commands, five shadow plus five opaque draw calls**. Thus mode/group-count variance is **not** the explanation for the observed performance differences. The synthetic scene and byte-identical raster parity do not prove real-game or cross-platform correctness; timings are synchronous CPU wall only.

### Actual CSV calculations, measured 81 frames (115..195)

```text
                             run1          run2         run3
render median ms             1.3936        1.3699       0.8277
backend (shadow+opaque) med  0.7241        0.7365       0.4745
other CPU median ms          0.6326        0.6176       0.3531
shadow backend median ms     0.4490        0.4657       0.2302
opaque backend median ms     0.2577        0.2622       0.2400
shadow phase median ms       0.7578        0.7495       0.3675
opaque phase median ms       0.3598        0.3561       0.3233
CPU outside 2 model phases   0.1693        0.1939       0.1352
frames render >1.2 ms        44/81         50/81         0/81
```

`backend` is computed **per frame** as `shadow_backend_ms+opaque_backend_ms`. `other CPU` is per-frame `render_cpu_ms-backend`, not GPU idle time. The medians of different columns need not sum exactly. Correlation of full render CPU with backend CPU across frames 115..195 is approximately **0.985 / 0.984 / 0.889** for run1/run2/run3 respectively, and with other CPU approximately **0.979 / 0.976 / 0.799**. These are mathematical correlations, not causal attribution.

To avoid comparing entire process baselines where fast/slow populations mix, each of the first two processes was also partitioned by **measured frame render_cpu_ms>1.2** (diagnostic threshold, not production SLA):

```text
                         run1 fast  run1 slow   run2 fast   run2 slow
frames                      37         44         31          50
render median ms           0.8386     1.6950     0.8150      1.6403
backend median ms          0.4759     0.9517     0.4798      0.9475
other CPU median ms        0.3616     0.7557     0.3333      0.6957
shadow backend median ms   0.2315     0.4803     0.2319      0.4883
opaque backend median ms   0.2441     0.4924     0.2426      0.4246
shadow phase median ms     0.3745     0.8243     0.3587      0.7950
opaque phase median ms     0.3293     0.6977     0.3197      0.6150
CPU outside model passes    0.1324     0.2102     0.1301      0.2200
```

**Key inference:** both CPU submit time and CPU remainder increase roughly twofold in slow frames, across both shadow and opaque passes, without any change in MDI command count. Thus the late spikes are **not isolated to the CPU ICB encode loop**. It is reasonable to investigate CPU thread scheduling, CPU frequency/thermal behavior, app/process co-tenancy, driver CPU contention and GPU queue waits. None has been directly measured; do not assert causation. The quick run3 provides a useful control showing a consistent ~0.83ms median, backend ~0.47ms and remainder ~0.35ms with exactly the same MDI workload.

Time-window patterns indicate bursts and recoveries within a run rather than an irreversible warmup change. Example run1 10-frame render medians: 115–124 ~0.844ms, 135–144 ~1.578ms, 145–154 ~1.643ms, 165–174 ~0.837ms, 185–194 ~1.50ms. Run2 115–124 ~1.542ms, 165–174 ~0.832ms, 185–194 ~1.559ms. Run3 windows remain ~0.82–0.85ms throughout.

### Validation and next step

**Actually performed:** loaded the exact three owner-uploaded CSVs and computed all tables directly in Python; updated this GitHub handoff. **Not performed:** native compilation/test on assistant side, OS thread state/frequency tracing, native Metal command buffer GPU durations, Vulkan parity, CI. No code change or promise of guaranteed speedup. PR #39 remains Draft and opt-in CPU MDI/AUTO defaults stay OFF.

**Best next experiment:** acquire a macOS Instruments Time Profiler + System Trace (or equivalent signposted CPU dispatch/wait + GPU timeline), ideally alongside the same 1k fixture, **for both a slow and fast run**. Quantify CPU thread on-CPU vs waiting and GPU queue backpressure. Separate direct stopwatch data from actual scheduling and GPU submission latency before performing further low-level Metal optimizations. If a capture is too invasive, add low-overhead, per-frame CPU thread-time versus wall-time counters plus opt-in GPU completion timestamps in another isolated branch stage with handoff and pixel parity tests. Avoid excessive per-submission log I/O in the hot loop, and test only with reusable, driver-safe Metal bindings.


---

## Stage 95 — measure render calling-thread CPU time against CPU wall time (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Commits:** `c28a0295de1974f25f9cbf16b3ddf9dc06ce08ea` (`tests/render/material_batches.cpp`), `842270065a108347687f68d2f4c79067877bc6c2` (`tools/batching-stage95-analyze.py`), `ebebdc4e72e39e0083a18d61b56e4e13a25c3357` (`tools/batching-stage95-trials.sh`). This handoff is its own subsequent commit. **Stage 95 new source is not yet compiled or executed natively.** Renderer/Metal backend/CPU AUTO policy unchanged.

### Motivation — actual observed performance variability

User supplied 3 complete 106-frame CSVs from previous real Mac Metal forced CPU MDI tests, where two showed process median `renderer.render()` around 1.37–1.39ms and the third ~0.83ms despite identical 81/81 MDI-active frames, ten submissions, 2048 indirect commands and 5 shadow + 5 opaque draw calls per frame. Within slow periods, both `shadow_backend_ms + opaque_backend_ms` and other synchronous CPU work roughly doubled, on both shadow and opaque. CPU thread descheduling, CPU frequency shifts, blocking inside Metal driver and thermal/contention are still **hypotheses**, not established facts. GPU execution duration and FPS remain unmeasured.

### New fixture

`tests/render/material_batches.cpp` adds explicit **opt-in** `AIVERSE_BATCH_THREAD_CPU_TIMING=1`, intended with `AIVERSE_BATCH_FRAME_TIMELINE=1` and `AIVERSE_PALETTE_TEST_CASE=auto_stress`. Around the same `renderer.render(pass)` call measured by `steady_clock`, it calls `clock_gettime(CLOCK_THREAD_CPUTIME_ID,...)` on macOS/Linux to sample CPU time accumulated by the **calling thread**. It computes thread CPU milliseconds from nanoseconds and off-CPU approximation `max(0, wall_ms - thread_ms)`. Unsupported clock readings write `-1`, clearly marking unavailable values instead of substituting process-wide CPU clocks. Windows and unsupported platforms leave the optional timing unavailable; existing raster/time fixture behavior remains unchanged.

When thread timing is explicitly ON, only then append two columns to the existing 10-field per-frame CSV (after the old fields): `render_thread_cpu_ms`, `render_offcpu_ms`. When flag is OFF, the previous CSV format is unchanged for backward compatibility. Samples still buffered in memory and only written after the second capture window. The timestamp overhead is within the wall interval but not the thread delta exactly; use repeated comparisons to assess effect. Calling-thread CPU time can be lower than wall due to CPU scheduling or blocking, but worker-thread parallel CPU time is excluded. The difference is **NOT** a direct GPU wait, a definite scheduler fault or GPU execution time. No RHI timing/event submission/command encoding changes.

### Analysis and runner tools

- New `tools/batching-stage95-analyze.py` uses Python 3 stdlib to read 3 (or arbitrary number) of Stage 95 CSVs. Validates 12 columns and 106 ordered frames 90..195, measured 115..195 with `mdi_submissions>0`, all nonnegative/finite CPU readings, and per-frame thread CPU no more than 0.15ms above the recorded wall CPU. Reports full MDI, fast MDI (<=1.2ms) and slow MDI (>1.2ms) median CPU wall, thread CPU, non-running/waiting difference, its P95, Metal backend CPU wall, and median off-CPU fraction. Writes `stage95-thread-summary.csv` beside input files. Threshold is a diagnostic split, **not an SLA or AUTO policy threshold**. Analyzer requires >=40 sampled MDI frames per input.
- `tools/batching-stage95-trials.sh` runs **three independent processes** of 1,024-item forced MDI, with CPU thread timing and buffered CSV, no GPU ICB or prior trace flags inherited. Requires CPU invariant pass, actual real-MDI screenshot parity, exactly 81 MDI-submitted measured frames, 106-frame CSV, and test program success; invokes analyzer and prints final summary location. Use `bash` since GitHub Contents API does not guarantee executable file mode.

### Validation truth

**Completed:** GitHub source writes and comparison with existing fixture/CSV analyzer contract; external documentation confirms macOS `CLOCK_THREAD_CPUTIME_ID` supports calling-thread CPU-time clock. **Not performed:** Stage 95 AppleClang compilation, Python analyzer test on Stage 95 output, repeated native timings, GPU timestamps, Instruments capture, Vulkan parity, full CI. The measured Stage 92 owner passes belong to prior code only. Default behavior remains untouched; PR #39 stays Draft.

### Safe Mac Studio steps (new isolated detached worktree)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage95
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage95" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage95" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage95/aiverse_material_batches \
  bash tools/batching-stage95-trials.sh build/batching-stage95/trials 1024
```

Each sample run should show `material palette CPU checks=38 result=passed` and `actual MDI pixel parity ... changed_channels=0` and the usual terminal `changed_channels=0`. The analyzer outputs `build/batching-stage95/trials/stage95-thread-summary.csv`. If an OS/thread clock is unavailable, `-1` should cause explicit analyzer failure, not a misleading result. Do not blindly destroy previous worktrees with in-progress rebase.

### Next decisions

If slow-frame **wall** increases while calling-thread **CPU time** stays near fast values, investigate descheduling or blocking/waiting with Instruments System Trace and Metal command buffer instrumentation. If thread CPU itself rises ~2x, inspect Xcode Time Profiler samples and CPU frequency/thermal/processor-core distribution; a thread CPU clock measures consumed time, not instructions or cycles. If both vary, compare per-stage CPU backend/other metrics and use paired fine-grained time profiling. In all cases validate the same byte-identical raster fixture, and eventually independent GPU timing with representative 10k workloads before any default enable/merge. Maintain separate handoff each stage.


### Stage 95 runner parity hardening

After the Stage 95 initial handoff, the standalone bash runner gained a hard assertion that the **terminal** pixel comparison line is `^changed_channels=0$` in addition to the separately verified actual-MDI capture. Source commit: `b36d8a5a23e7213b5966678dcece0c85d719feca`. This assertion was reviewed at source level but **not run on Mac**. Native compilation and validation remain pending.

**Note:** The direct CPU-thread clock uses `CLOCK_THREAD_CPUTIME_ID` on macOS/Linux (per platform documentation), and the CSV intentionally retains its first 10 legacy fields. The new analyzer requires actual populated thread-time columns and rejects an unsupported `-1` value. Do not mistake the thread-vs-wall difference for a GPU timestamp or conclusive evidence of OS descheduling.


---

## Stage 96 — repair Stage 95 bash runner syntax and duplicated tail (2026-11-20)

**Repository / branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Source fix:** `ef583ec975746b3e09e0ad26614d914759351ffc` in `tools/batching-stage95-trials.sh`. This handoff is a separate commit.

### Actual user Mac Studio result

The owner successfully configured and built `aiverse_material_batches` with AppleClang 17, including the new Stage 95 per-thread timing fixture, ending in `[100%] Built target aiverse_material_batches`. The subsequent shell script immediately failed:

```text
tools/batching-stage95-trials.sh: line 45: unexpected EOF while looking for matching `''
```

Inspection of the actual branch file found a **truncated shell line 36** `grep -q '^changed_channels=0` missing the closing quote / log argument, followed by a **duplicated malformed tail** after the first analyzer call. This was introduced in the previous runner hardening update. The user has not executed any Stage 95 forced-MDI trials after the syntax error; no new CPU thread/wall measurements or screenshot parity can be claimed.

### Implemented repair

Replaced the malformed runner tail with complete, single sequence:
1. Assert CPU checks PASS.
2. Assert captured actual CPU MDI frame parity `changed_channels=0`.
3. Assert terminal image parity via `grep -q '^changed_channels=0$' "$name.log"`.
4. Assert forced CPU MDI in all 81 measured frames.
5. Assert buffered CSV emitted 106 frames and the output file exists.
6. Close `for` loop exactly once, run the Stage 95 Python analyzer once, print the success message once.

GitHub source rewrite succeeded; a Bash `bash -n` syntax check of a local reconstruction of the corrected runner completed PASS. **Not performed:** native Mac execution of repaired runner, renderer screenshots/benchmark, and CI. No C++ or Metal runtime changes in Stage 96.

### Re-run on Mac Studio without rebuild or checkout mutation

The owner's `AIVERSE-batching-stage95` worktree already has a successfully compiled Stage 95 executable, and some prior worktrees had unfinished rebase/merge operations. Do **not** reset or switch that worktree. Fetch and materialize **only the repaired shell runner** into a temporary file within `tools/`, because the script uses `cd "$(dirname "$0")/.."` to find project root:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage95 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage95-trials.sh \
  > tools/batching-stage95-trials-fixed.sh || exit 1
bash -n tools/batching-stage95-trials-fixed.sh || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage95/aiverse_material_batches \
  bash tools/batching-stage95-trials-fixed.sh build/batching-stage95/trials 1024
```

If writing an untracked file to this worktree is undesirable, use a new clean detached worktree and its own separate CMake build; do not use `git reset --hard`. Expected: three real CPU MDI runs, 38 CPU checks and two pixel parity assertions per process, and `stage95-thread-summary.csv` showing `wall_median_ms`, `thread_median_ms`, `offcpu_median_ms`. Any analyzer `-1` unsupported clock or validation failure must remain a failure, not be silently suppressed. GPU timing/FPS remain unmeasured. Preserve Draft PR #39, CPU MDI/AUTO disabled by default.

### Next agent step

Inspect actual Stage 95 `stage95-thread-summary.csv` after Mac native run. Compare fast vs slow groups: if thread CPU stays constant while wall increases, investigate CPU blocking or descheduling; if thread CPU doubles, profile CPU execution/frequency with Instruments; if both, investigate both. Record results with a new handoff before any performance change.


---

## Stage 97 — Stage 95 thread CPU clock anomaly; independent macOS clock control (2026-11-20)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39.
**Code commit:** `ad261a6c45fc06c25fb048a6ad1a6112a9187dc2` introduces `tools/batching-thread-clock-sanity.cpp`. This handoff is a separate commit. **No renderer code, policy or Metal bindings changed in this stage.**

### Exact owner uploads and observations

Owner uploaded `mdi-1-frames(1).csv`, `mdi-2-frames(1).csv`, `mdi-3-frames(1).csv` and `stage95-thread-summary.csv`. Python parsed all three raw 106-row, 12-column files. Frame 115..195 in each: 81 frames with fixed 10 CPU MDI submissions, 2048 indirect commands. The owner's analyzer reported:

```text
           mdi-1       mdi-2       mdi-3
wall med    0.854208    0.843959    0.934708  ms
thread med  0.020464    0.020225    0.022390  ms
offcpu med  0.833746    0.823734    0.912318  ms
backend med 0.500458    0.481290    0.558415  ms
```

All three Stage 95 runs were relatively fast (no frames exceeding the 1.2ms slow cutoff). The unusually tiny *reported* calling-thread CPU time is suspicious: across all 243 measured frames, `render_thread_cpu_ms/render_cpu_ms` is almost a constant **~0.02396**; per-run median 0.023959 / 0.023961 / 0.023958, variation of the ratio within each run on the order of 4e-5 to 5e-5. Correlation of thread and wall readings across frames per run ~0.9983 / 0.9988 / 0.9986. A nearly constant ratio of 1:41.7 over different process runs is unlikely to be direct evidence of random descheduling. It strongly motivates checking clock scaling/units/platform implementation **before interpreting Stage 95 `render_offcpu_ms`** as actual off-CPU. It might also reflect unusual threading/wait behavior, but root cause is not known.

**Do not claim macOS descheduled the renderer for 97.6% of its wall time.** Stage 95 thread CPU measurements are **provisionally unvalidated** pending independent control. The `render_cpu_ms` wall readings and existing per-stage CPU wall samples remain valid as such. No GPU execution/FPS data collected.

### New independent diagnostic

`tools/batching-thread-clock-sanity.cpp` is a standalone CPU-only macOS C++ program: measures a ~30ms non-sleeping xorshift arithmetic loop with `steady_clock`, `CLOCK_THREAD_CPUTIME_ID`, `CLOCK_PROCESS_CPUTIME_ID`, plus Darwin Mach `THREAD_BASIC_INFO` user+system CPU times. It prints all the deltas and ratios to wall, and returns nonzero if the busy-loop thread CPU clock is implausibly small (<50%) or large (>120%) relative to wall. No Metal dependencies or benchmarking code touched; standalone compile and actual hardware execution still **not performed** by assistant.

### Next Mac Studio step (no rebase or engine rebuild)

Existing Stage 95 detached worktree already compiled `aiverse_material_batches`. Fetch and extract just this new file without switching branches, compile a standalone tiny diagnostic with AppleClang, then run:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage95 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-thread-clock-sanity.cpp > /tmp/aiverse-clock-sanity.cpp || exit 1
clang++ -std=c++17 -O2 -Wall -Wextra -Werror /tmp/aiverse-clock-sanity.cpp -o /tmp/aiverse-clock-sanity || exit 1
/tmp/aiverse-clock-sanity
```

Send both printed `Clock sanity` and `Mach THREAD_BASIC_INFO` lines and return code. If this busy-loop thread clock also reads ~2.4% of wall, investigate clock semantics/scaling and calibrate with native Mach/thread info before trusting Stage 95 off-CPU. If busy-loop clock agrees with wall, then explore whether renderer intentionally blocks, submits work on a separate worker, or enters Metal driver APIs that wait; corroborate with Instruments/System Trace to avoid mistaking a clock path artifact. Also capture a *slow* Stage 95 run in addition to fast ones before causal performance claims. Do not silently multiply observed CPU time by 41.7 without validating the source of the ratio.

### Validation truth

**Performed:** raw owner CSV numeric analysis; GitHub standalone diagnostic committed; handoff updated. **Not performed:** macOS native compilation and running the standalone control, GPU timing, Mach/System Trace, full CTest or Vulkan. PR #39 remains Draft, all diagnostic flags default off.


---

## Stage 98 — validate Stage 95 thread-accounting within actual render fixture (2026-11-20)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Code commits:** `1b0356b021c0f3baf10dfc04c40666b8963e399c` in `tests/render/material_batches.cpp`; `04f07ee36f7bf8111c862b532d8fc49931f5fd6f` in `tools/batching-stage95-analyze.py`. This handoff is a separate subsequent commit. All changes in diagnostic fixture/analyzer, **no renderer/Metal/AUTO policy changes**.

### Owner Mac Studio Stage 97 control — PASS

The owner fetched and natively compiled `tools/batching-thread-clock-sanity.cpp` with AppleClang and ran the 30ms busy loop. Actual output:

```text
Clock sanity (CPU-bound, no Metal): wall_ms=30.016291
thread_cpu_ms=29.997417 process_cpu_ms=29.998000
thread_to_wall=0.999371 process_to_wall=0.999391
iterations=4419 checksum=16553240126093259979
Mach THREAD_BASIC_INFO delta_ms=29.998000 mach_to_wall=0.999391
Clock sanity PASS: busy-loop thread CPU and wall clocks agree
```

Thus the Darwin `CLOCK_THREAD_CPUTIME_ID` clock is not globally scaled by ~1/41.7; both POSIX thread CPU and Mach thread basic info match steady-wall during CPU-bound work. This does **not** prove that the 0.02ms thread-time measured around `renderer.render()` in Stage 95 represents descheduling: it could be waits/blocking/other threads/driver semantics or a measurement artifact localized to the renderer.

### Previous observed Stage 95 anomaly

Owner supplied all three Stage 95 106-row CSVs; 81 measured MDI frames each, 10 submissions + 2048 commands every measured frame. Median `renderer.render()` wall-time: 0.8542 / 0.8440 / 0.9347ms. Median **reported** calling-thread CPU-time: 0.0205 / 0.0202 / 0.0224ms. The ratio `thread/wall` was exceptionally stable near 0.02396 in *every* measured frame, across all three processes; this is suspicious and requires independent validation. 1.2ms slow interval was not reproduced in these three test runs. CPU wall-time is not GPU execution time.

Source inspection established that the wall start and end timestamps enclose `renderer.render(pass)`, and the POSIX calling-thread CPU timestamps were also sampled directly on either side of that same call. `Renderer3D::render` invokes model rendering synchronously; no evidence of a dedicated worker thread can be asserted merely from looking at this outer method.

### Stage 98 diagnostics

- In the existing **explicitly opt-in** `AIVERSE_BATCH_THREAD_CPU_TIMING=1` path around the identical `renderer.render(pass)`, also sample `CLOCK_PROCESS_CPUTIME_ID` on macOS/Linux and `Mach THREAD_BASIC_INFO` on macOS for the *same calling thread*. Verify process CPU and Mach thread CPU are independent cross-checks against POSIX thread CPU and steady wall time.
- Append two more CSV columns **only when the opt-in flag is set**: `render_process_cpu_ms`, `render_mach_thread_cpu_ms`. Missing unsupported measurements get `-1` (Mach on non-Apple platforms). The original 10 default CSV columns are unchanged, and the first two Stage95 optional columns remain in the same positions.
- Extend `tools/batching-stage95-analyze.py` to accept these extra optional fields and print `procCPU`, `machThr` medians, writing them to `stage95-thread-summary.csv`. Older Stage95 12-column CSVs still parse with NaN for unavailable optional measures. This analyzer independently rejects invalid negative values in optional fields and the pathological case that measured process CPU is much below thread CPU; it does not conflate process CPU with renderer CPU due to possible other threads.
- Measurements include the minimal clock-probe overhead in their wall region; used for **comparative diagnosis**, not a production performance claim. Runtime Metal paths/default-off feature flags unchanged.

### Source and validation truth

**Performed:** owner native Stage97 clock control PASS; GitHub source inspection and Stage98 fixture/analyzer updates. **Not yet performed:** Stage98 AppleClang build, Stage98 native run, analysis of Stage98 14-column output, GPU completion timing, macOS scheduler trace, Vulkan tests, full CTest. Keep PR #39 Draft.

### Exact next Mac Studio steps — new detached worktree

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage98
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage98" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage98" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage98/aiverse_material_batches \
  bash tools/batching-stage95-trials.sh build/batching-stage98/trials 1024
```

Expected runner builds three forced real CPU MDI trials, asserts native CPU checks, raster parity and 81/81 measured CPU MDI frames, and analyzes each measured frame's `wall`, `thread`, `procCPU`, `machThr`. If the earlier ~0.024 ratio persists and Mach agrees with POSIX thread while process CPU is much larger, consider work happening on other threads; if Mach and POSIX agree and process CPU is also tiny, investigate blocking/driver waits with Instruments. If Mach and POSIX disagree, investigate per-clock measurement semantics and/or fixture instrumentation. If thread CPU approaches wall, the earlier Stage95 anomaly was transient; use system trace to explain differences. Do not claim causality based on one process. Preserve all older worktrees and Git state.


### Stage 98 portability correction

Source `da49fb94074dc1cab1295ed6aaf701a857061536` changes `cpuClockNs` to take a simple `bool processClock` argument rather than a cross-platform `clockid_t` parameter. The POSIX `clockid_t` type and `CLOCK_{THREAD,PROCESS}_CPUTIME_ID` references are now inside Apple/Linux compile guards. Windows builds still mark the optional clock unavailable, as before, rather than failing to parse the fixture. Source-level correction only; native compilation and runtime validation remain pending.


---

## Stage 99 — Stage 98 Mac thread accounting disproves claimed 97.6% off-CPU; fix diagnostics (2026-11-20)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Code commits:** `647ba1c1aef08cffabe1a959e8ec70478f14b08a` in `tests/render/material_batches.cpp` and `3aefac4c1983d03f48bf2b859cf3954cda0fd2e8` in `tools/batching-stage95-analyze.py`. This handoff follows separately. No renderer, Metal binding, CPU MDI encoding, AUTO selection, or GPU runtime code changes.

### Real owner-provided Stage 98 Mac result

Owner uploaded `stage95-thread-summary(1).csv` from the Stage 98 fixture (3 distinct forced MDI processes; 81 measured frames each). Exact medians, milliseconds:

| Metric | MDI1 | MDI2 | MDI3 |
|---|---:|---:|---:|
| Wall time | 0.796541 | 0.812584 | 0.860833 |
| Raw POSIX thread CPU | 0.018933 | 0.019307 | 0.020444 |
| Previously calculated `offcpu` (INVALID) | 0.777585 | 0.793259 | 0.840432 |
| Process CPU | 0.792 | 0.810 | 0.854 |
| Mach thread CPU | 0.786 | 0.802 | 0.850 |
| Backend CPU wall | 0.454166 | 0.463251 | 0.498332 |

All three test runs fell entirely into the fast (<=1.2ms) population, with 81 measured MDI frames each; no slow MDI evidence from this set. Crucially, independent **Mach per-thread** CPU and **process** CPU both closely track wall time (~0.8ms), whereas the POSIX thread clock is about **41.5x smaller** despite a prior standalone CPU-bound clock control showing it behaved normally. There is an unresolved clock-accounting inconsistency specifically in the renderer fixture / timing context. **Do not interpret the raw POSIX-derived 97.6% offcpu as real wait, descheduling or GPU blocking.** The Stage 98 source and medians do not establish why this inconsistency occurs. The independently measured Mach thread CPU matches the process CPU and wall and is the preferable macOS measure for this fixture. There is no GPU execution/FPS timing.

### Stage 99 fix and API

- `tests/render/material_batches.cpp`: when Stage95 opt-in `AIVERSE_BATCH_THREAD_CPU_TIMING=1` is enabled, `render_offcpu_ms` now derives from `render_mach_thread_cpu_ms` **on macOS** (fallback unavailable -> `-1`), and from POSIX thread CPU on other supported platforms. `render_thread_cpu_ms` keeps its original **raw POSIX** reading for forensic diagnosis. `render_process_cpu_ms` and `render_mach_thread_cpu_ms` remain separate in CSV; no change to default ten columns without opt-in.
- `tools/batching-stage95-analyze.py`: when a Mach column exists, reject negative/unavailable Mach measurements, recompute offcpu **from wall - Mach** for every row, and display `thread_median_ms` using trusted Mach time. It prints `posix_thread_median_ms` as a separate diagnostic, so older 14-column Stage98 CSVs can be safely reanalyzed without rerunning the Mac renderer. For older 12-column CSVs, it continues using POSIX as a *provisional unvalidated* measure; do not draw wait conclusions from those older files. The stage95 summary now includes `posix_thread_median_ms`. Historical `render_offcpu_ms` in Stage98 raw files is explicitly ignored when Mach exists.
- No performance policy changes and no assumption that measured wall-Mach equals GPU runtime or pure scheduler waiting.

**Validation:** user-provided Stage98 analyzer CSV inspected; Stage97 standalone native CPU sanity test previously PASS; GitHub source edits completed. **NOT YET verified:** Stage99 source compile, executable tests, rerunning the updated Python analyzer on raw Stage98 14-column files, GPU timer or macOS scheduler traces. Keep PR Draft.

### Safe next step without recompilation

On existing `AIVERSE-batching-stage98` detached worktree, retrieve only the updated Python analyzer into `/tmp` and rerun on already-existing three Stage98 frame CSVs:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage98 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage95-analyze.py > /tmp/aiverse-stage99-analyze.py || exit 1
python3 /tmp/aiverse-stage99-analyze.py \
  build/batching-stage98/trials/mdi-1-frames.csv \
  build/batching-stage98/trials/mdi-2-frames.csv \
  build/batching-stage98/trials/mdi-3-frames.csv
```

Expected: thread CPU ~0.79/0.80/0.85ms, `offcpu` near zero (~0.01ms), raw POSIX near 0.02ms. If investigating the patched fixture too, build it in a fresh `AIVERSE-batching-stage99` detached worktree and rerun native tests, or compile its changed C++ fixture separately after safely fetching the new branch; do not switch an existing rebasing worktree.

**Next engineering decision:** capture a slow Stage99 run if reproducible; compare Mach thread time vs wall and the same backend CPU submission stages. A slow run with wall and Mach both rising suggests more actual CPU cost/clock-frequency changes rather than prolonged off-CPU waiting; a slow run with wall rising but Mach not rising motivates Instruments System Trace or Metal driver wait tracing. Further low-level Metal optimization should be evidence-based and retain pixel parity and other-platform builds.


---

## Stage 100 — Mach CPU confirms near-continuous on-CPU in three fast runs; bounded slow-run capture (2026-11-20)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/auto-batch-cost-metrics-20261119`; **Draft PR:** #39. **Code:** `c0180eb2685ce3f014b2faea1cdb71ebc8604192` creates `tools/batching-stage100-capture-slow.sh`. This handoff is a subsequent commit. No Metal rendering, RHI, AUTO, model batching, uniform bindings, or default flags altered.

### Actual Mac Studio owner test

The owner fetched the Stage99 fixed Python analyzer without changing their Stage98 detached checkout, reran it on the *same three existing* 106-row Stage98 CSVs, and provided output:

```text
trial  measured     wall ms  trusted Mach thread ms  offcpu ms  backend CPU wall ms  process CPU ms   raw POSIX ms
mdi1   81/81        0.7965       0.7860             0.0105       0.4542            0.7920          0.0189
mdi2   81/81        0.8126       0.8020             0.0101       0.4633            0.8100          0.0193
mdi3   81/81        0.8608       0.8500             0.0114       0.4983            0.8540          0.0204
```

No slow frames (>1.2ms wall) in any of these three measured sample windows. The previous `offcpu ~= 0.8ms` was a **measurement error** from the anomalous POSIX `CLOCK_THREAD_CPUTIME_ID` reading in this specific fixture; independent Mach per-thread CPU, process CPU, and wall track one another closely. The standalone CPU-bound Stage97 clock test also passed, so precise cause of POSIX mismatch in the Metal fixture is unresolved. **Do not claim 97.6% descheduling**. Current observations imply only ~0.01ms wall-minus-Mach per frame in **fast** runs. They do not explain previously observed *slow* forced-MDI processes, nor prove absence of GPU waits. All measurements are CPU only, no GPU timestamps/FPS.

### New runner / API

`tools/batching-stage100-capture-slow.sh` runs independently the already-built Stage98 native `aiverse_material_batches` process with forced CPU MDI, the same synthetic 1,024 primitives, and opt-in per-frame Mach/CPU and timing CSV. Uses **max eight processes by default** (user can pass a count 1..20). For each run, checks 38 CPU invariants via the printed result, pixel parity for actual MDI capture and final image, all 81 measured frames MDI with exactly 810 real submissions, and nonempty CSV. Then validates 106 ordered frames 90..195 and 81 measured frames 115..195 with exactly 10 submissions and 2048 commands each. Counts frames exceeding a *diagnostic*, not SLA, CPU wall threshold 1.2ms. **Stops at the first process with at least 10 slow measured frames** and preserves that process's native log and 106-row timeline. Writes `summary.csv` with run number, slow frame count, median CPU wall, Mach thread CPU, computed wall-minus-Mach offcpu, and CPU backend wall. If no run reproduces the slowdown, prints `INCONCLUSIVE` and retains all logs/CSVs; does not claim a fix or successful root-cause diagnosis.

Runner uses `set -euo pipefail` and does not switch branches, reset conflicting worktrees, run a build or modify production source. All opt-in diagnostics and forced MDI remain off by default for actual engine runtime. A previous locally constructed equivalent shell script was checked by `bash -n`; **the exact committed Stage100 script has not been independently run on the owner's Mac or full native fixture**, so remain cautious and correct any parsing issues based on actual output.

### Mac Studio command (use existing Stage98 compiled executable; no new worktree/build)

The script resolves project root relative to its location inside `tools/`. Fetch only the runner into an untracked file with that directory, syntax-check, execute a bounded run:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage98 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage100-capture-slow.sh \
  > tools/batching-stage100-capture-slow-local.sh || exit 1
bash -n tools/batching-stage100-capture-slow-local.sh || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage98/aiverse_material_batches \
  bash tools/batching-stage100-capture-slow-local.sh build/batching-stage98/slow-capture 8
```

Send summary and **one slow process's full `mdi-N-frames.csv` if found**. If no slow process, report the negative result; the intermittent issue remains unproven during that batch. Next investigate whether slow-run Mach thread CPU doubles together with wall and backend CPU: if it does, pursue Instruments CPU Time Profiler, CPU frequency/thermal/driver CPU cost; if wall alone doubles, pursue blocking/descheduling and System Trace. Do not modify AUTO thresholds or force-enable MDI until repeatable cross-platform perf and pixel parity are verified. Keep Draft PR #39.


---

## Stage 101 — three new uploads are the earlier Stage 98 fast timelines, not evidence of a slow Stage 100 capture (2026-11-20)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Type:** documentation/evidence-only. No renderer/Metal RHI, AUTO, benchmark implementation or CI modifications.

The user uploaded `mdi-1-frames(2).csv`, `mdi-2-frames(2).csv`, `mdi-3-frames(2).csv`. The assistant loaded all three exact files with Python and verified each has 106 rows (frame 90..195), 14 columns including Mach thread and process CPU. For each measured frame 115..195, `mdi_submissions=10` and `mdi_commands=2048` (81 frames per file). All three contain **zero** measured frames with `render_cpu_ms>1.2` ms.

| Run | Wall median ms | Mach thread median ms | Metal backend median ms | Slow frames |
|---|---:|---:|---:|---:|
| mdi-1 | 0.79654 | 0.786 | 0.4542 | 0/81 |
| mdi-2 | 0.81258 | 0.802 | 0.4633 | 0/81 |
| mdi-3 | 0.86083 | 0.850 | 0.4983 | 0/81 |

These medians and the 14-column schema match the **previously analyzed Stage 98 three fast runs**, not a freshly reproduced slow Stage 100 run. Without new log/runner `summary.csv` metadata we should not assert that the bounded Stage100 runner was actually executed; merely re-uploading copies of these three CSVs adds no new slow-frame evidence.

**Important:** the uploaded raw `render_offcpu_ms` column uses the now-rejected POSIX thread CPU time clock (~0.02ms) rather than validated Mach. Ignore that raw column; the Stage99 Python analyzer recomputes offcpu using `max(0, wall - Mach)` if the Mach column exists, yielding medians about 0.0105/0.0101/0.0114ms in the existing Stage99 output.

**Verification truth:** 3 CSV files read and parsed directly, numbers cross-checked against prior Stage98/99 owner evidence. **Not verified:** a new Stage100 native run, native build in this turn, slow-process system trace, GPU execution timing or performance stability at other loads. Existing Stage100 script remains the next diagnostic mechanism. Keep PR #39 Draft.

**Next required artifact:** run the bounded Stage100 script using the already-built Stage98 fixture and share `build/batching-stage98/slow-capture/summary.csv`; if `slow_frames>=10` in any process, share that run's `mdi-N-frames.csv`. Do not infer cause from three old fast runs.


---

## Stage 102 — unique archived native CPU MDI capture workflow (2026-11-20)

**Repository:** `chromealex/AIVERSE`, **branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Source commit:** `c48187535d52d2b01fa5932b36bf8e154576ae3c` adds `tools/batching-stage102-repro.sh`. This handoff is a separate commit. No renderer/Metal/RHI/AUTO code changed; new script invokes existing Stage98 opt-in fixture.

### Why this stage

The most recent user-uploaded files `mdi-1-frames(2).csv`, `mdi-2-frames(2).csv`, `mdi-3-frames(2).csv` have 106 rows, fixed 81 MDI frames at 10 submissions/2048 commands, all <1.2ms, and medians precisely matching prior Stage98 fast three runs: 0.79654/0.81258/0.86083ms. They are **not verified** as newly generated Stage100 output; repeat uploads alone cannot establish new slow-run evidence. A unique run directory and manifest/archive prevent confusion.

### New capture tool

- Script runs from `tools/` using repository-relative project root, default up to 8 independent forced MDI process executions; integer run limit 1..20.
- Each invocation creates unique `build/batching-stage98/stage102/capture-YYYYMMDDTHHMMSSZ-PID/`; never overwrites previous CSV. A manifest records start/end UTC, detached Git HEAD, binary path, input scene, threshold and status.
- Each process runs 1,024 unique primitives, 81 measured frames, opt-in `AIVERSE_BATCH_THREAD_CPU_TIMING=1` and deferred per-frame CSV. It explicitly suppresses inherited AUTO/GPU ICB and per-submission trace env flags.
- Performs source-level log assertions for 38 CPU invariant pass, real-MDI capture parity zero, final parity zero, all 81 MDI frames with exactly 810 submitted MDI groups, 106 captured frames, and nonempty CSV. Data validation checks exact frame 90..195 sequence, each measured frame having 10 submissions and 2048 commands, finite nonnegative wall/Mach/backend columns.
- Writes `summary.csv` with run number, slow count (>1.2ms CPU `render()` wall), median wall, median validated Mach thread CPU, wall-minus-Mach and backend CPU; stops as soon as 10+ slow measured frames appear in one process. If not, prints inconclusive rather than declaring fixed.
- Captures unmodified `mdi-N.log` and `mdi-N-frames.csv` for each run. Produces **one `capture-....tar.gz` file** containing summary, manifest, all logs and CSV for easy transfer. No need to upload unrelated stale same-name files.
- Runner is diagnostic, CPU wall only, no GPU timestamp/FPS or OS state trace. The cross-run 10+ slow threshold is exploratory and **not** a production SLA.

### Validation performed vs pending

**Performed:** GitHub repository source read to locate Stage100 runner contract, GitHub creation of Stage102 script, handoff update. **NOT performed:** exact Stage102 script `bash -n` on Mac or native execution, 8-run experiment, metal GPU timer, cross-platform GPU test. Prior Stage98 owner native test and Stage99 analyzer **PASS**, but those do not validate this new shell script.

### Exact safe Mac Studio invocation without rebuild or rebase

Use existing detached Stage98 worktree and compiled benchmark; do not switch/reset any existing branch or rebase state. Fetch the script as an untracked script under `tools/` so its path-relative `cd` resolves correctly:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage98 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage102-repro.sh \
  > tools/batching-stage102-repro-local.sh || exit 1
bash -n tools/batching-stage102-repro-local.sh || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage98/aiverse_material_batches \
  bash tools/batching-stage102-repro-local.sh 8
```

The console prints the unique `Stage 102 summary:` and `Stage 102 archive:` paths plus whether a slow run reproduced. Ask for the **generated archive** and/or its summary. If the benchmark or Python assertions fail, share its current unique run `mdi-N.log`; never claim a successful trial. Upon receiving a confirmed slow CSV, compare medians of fast vs slow **Mach CPU vs wall CPU**, and backend wall vs other CPU. If Mach grows with wall, likely more CPU work or frequency/driver cost; if wall rises without Mach, investigate thread blocking and Instruments System Trace. Do not infer GPU cause without GPU timestamps. Maintain Draft PR #39.


---

## Stage 103 — actual Stage 102 archive: 8/8 fast forced CPU MDI runs (2026-11-20)

**Repository:** `chromealex/AIVERSE`, **branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Evidence source:** owner-uploaded `capture-20261120T071121Z-92097.tar.gz`, containing a manifest, 8 process logs, 8 per-frame CSV files and PPM raster captures. **Changes:** handoff only; no production code, benchmark or test changes in this stage.

### Provenance and test configuration

The archive manifest confirms `stage=102`, UTC start `20261120T071121Z`, UTC end `20261120T071142Z`, Git HEAD `e2be90ee329f2a1268ced9175719ca6e719afcf4` (the **Stage 98 executable source checkout**, not the later diagnostic script commit), binary `build/batching-stage98/aiverse_material_batches`, `max_runs=8`, `items=1024`, diagnostic slow threshold `1.2ms` and slow-run detection threshold `10 slow frames`. Manifest result is `INCONCLUSIVE_NO_SLOW_RUN`.

The assistant directly parsed **all eight** `mdi-N-frames.csv` from the uploaded archive and checked each has 81 measured frames 115..195, all with `mdi_submissions=10` and `mdi_commands=2048`. Total sample 648 MDI frames and **zero** frames with `renderer.render()` CPU wall >1.2ms. Maximum observed measured CPU wall frame across all eight was **1.17342ms** (run 4). Median across all 648 frames was **0.8466875ms**.

### Exact measured per-run CPU medians in ms

| Run | CPU wall median | Mach calling-thread CPU median | CPU backend submission median | Slow frames |
|---|---:|---:|---:|---:|
| 1 | 0.850084 | 0.837000 | 0.488000 | 0 |
| 2 | 0.866583 | 0.855000 | 0.514542 | 0 |
| 3 | 0.839834 | 0.826000 | 0.479373 | 0 |
| 4 | 0.808375 | 0.797000 | 0.475834 | 0 |
| 5 | 0.875625 | 0.865000 | 0.526748 | 0 |
| 6 | 0.822667 | 0.810000 | 0.476126 | 0 |
| 7 | 0.900166 | 0.887000 | 0.543082 | 0 |
| 8 | 0.801792 | 0.791000 | 0.482709 | 0 |

Per-run median off-CPU from **wall minus Mach** was 0.0100–0.0136ms. This supports that the fixture calling thread was largely on-CPU for these *fast* runs; it does not prove GPU queue idle or eliminate scheduling/frequency as causes of historical slow runs. **Do not use legacy POSIX raw offcpu values**, which were shown to be wrong in this Metal fixture.

The logs include `material palette CPU checks=38 result=passed`, `actual MDI pixel parity frame=100 changed_channels=0`, measured 81/81 forced CPU MDI frames with 810 real submissions, and final `changed_channels=0` for each run. No renderer/GPU faults were reported. All pass assertions also required by the Stage102 script.

### Conclusion and next technical work

This is credible **new** test provenance, unlike earlier resubmissions of Stage98 CSVs. Eight independent processes all show CPU MDI wall ~0.80–0.90ms median on the 1k fixture, with 0/648 slow frames. However prior Mac tests recorded genuine forced-MDI medians ~1.37–1.60ms and dual shadow/opaque CPU stage increases. We cannot prove an actual performance bug is fixed, nor infer OS/Metal root cause from these eight fast processes.

Recommended shift in test strategy: instead of indefinite repetition of the same 1k test, test representative 2k and 4k primitive loads in a bounded *matched* Direct vs MDI experiment; keep constant scene and pixel parity, capture full CPU wall distributions, actual MDI submissions, and Mach CPU. Eventually add native GPU completion timing/memory/thermal metrics and test 10k after controlling CPU/GPU workload. Any claim of improvement is limited to sampled CPU `renderer.render()` wall, not FPS or GPU. PR #39 remains Draft, CPU MDI/AUTO features opt-in by default, and all existing worktrees/rebase states should be preserved.

### Validation truth

**Actually performed:** extracted exact owner archive, read all 8 per-frame CSVs, checked frame count/MDI count/command count and computed pooled/run medians and worst case, read manifest/representative native log, documented evidence. **Not performed:** new native compilation in assistant environment, full CTest, GPU timestamp, Vulkan/GLES parity, representative 2k/4k load test, any production code changes.


---

## Stage 104 — matched native Direct vs CPU MDI scaling on 2K/4K primitives (2026-11-20)

**Repo / branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, **Draft PR #39**.
**Commits:** `2cf5930847106400109b5ef4a791ced6ee0b4c13` adds `tools/batching-stage104-analyze.py`; `607a2f88f389754ce3c004e289834eb2ba07fc08` adds `tools/batching-stage104-trials.sh`. This handoff is a separate follow-up commit.

### Motivation / verified earlier owner evidence

Stage 103 extracted 8 real Mac Studio forced CPU MDI processes from archive `capture-20261120T071121Z-92097.tar.gz`. All 648 measured 1K-primitive MDI frames (81 per process) had 10 MDI submissions and 2048 commands per measured frame, no frames exceeding 1.2ms, pooled median CPU `renderer.render()` wall `0.8466875ms`. Each test passed 38 CPU invariants and actual-MDI/final image parity; Mach thread CPU closely followed wall. The owner previously also recorded intermittent slower 1K forced-MDI runs, so no claim of definitive resolution or always-fast behavior is justified. There is still no GPU duration/FPS/large-production-scene evidence.

### New tools and architecture

**No C++ or renderer code changed in Stage 104.** Existing `tests/render/material_batches.cpp` already accepts `AIVERSE_BATCH_STRESS_ITEMS` in 64..100000 and supports `AIVERSE_BATCH_STRESS_POLICY=direct|mdi`, optional `AIVERSE_BATCH_THREAD_CPU_TIMING=1`, deferred per-frame CSV and raster snapshots. In `direct` mode, the fixture disables batch-model MDI and should submit 0 indirect commands; in `mdi` mode it should submit the full 2x shadow+opaque primitive command count.

`tools/batching-stage104-trials.sh`:
- Tests **2,048 and 4,096 unique primitives** using separate processes in order **Direct1 / MDI1 / MDI2 / Direct2** for each size (8 native processes total). This is a matched process-order control, not randomized per-frame A/B/A.
- Explicitly disables inherited experimental GPU ICB, AUTO, profiling logs, and sets forced policy and CPU MDI flags; enables Mach CPU + synchronous wall and per-frame buffered CSV.
- Requires native CPU invariants result PASS, end-image pixel parity `changed_channels=0`, 106-frame timeline and a nonempty CSV. For MDI, also requires actual successful real MDI screenshot parity, 81 MDI-active frames and zero Direct measured frames. For Direct, requires 81 Direct measured frames and zero indirect commands/submissions. The latter mode must **not** set the actual MDI capture flag.
- Creates a **unique non-overwriting run directory** `build/batching-stage98/stage104/capture-UTC-PID` with 8 per-process logs and frame timelines, a provenance manifest (git checkout head, binary, sizes, order), the aggregate report and one `tar.gz` bundle.
- Uses the same pre-built Stage98 fixture on the owner's Mac; no rebuild should be necessary because Stage104 modifies scripts only.

`tools/batching-stage104-analyze.py`:
- Validates each CSV has 106 ordered frames 90..195 and 81 measured frames 115..195.
- Validates finite/nonnegative wall/Mach/backend and actual mode: Direct must have **0** indirect commands/submissions; CPU MDI must have >0 submissions and **2 * items** commands per measured frame (shadow + opaque).
- Computes CPU `renderer.render()` median, P95, max; Mach calling-thread CPU median; wall-minus-Mach offcpu median; CPU backend submission median; and group/command counts.
- Computes the difference of the medians of the two process medians for each mode, then reports `(MDI-Direct)/Direct*100%` with negative meaning lower measured CPU wall for MDI. These metrics must **not** be presented as GPU execution time/FPS.
- Saves `stage104-summary.csv` with all eight per-run entries, and prints CPU wall comparisons for 2K and 4K.

### Validation truth

**Performed:** read code of existing fixture's stress policy and input parsing, created both Stage104 tools via GitHub, updated handoff. **Not performed:** the new runner's native Mac execution, native Direct 2K/4K parity, `bash -n` on Mac, script analyzer with actual 2K/4K CSV, 4K native performance, GPU timestamp collection, full CTest, Vulkan/GLES parity. If the expected 2*items MDI command count differs due to legitimate chunking, record actual CSV and adapt the validation only with evidence; do not hide errors. PR #39 remains Draft, production flags remain OFF by default.

### Exact Mac Studio invocation (without switching checkout or rebuilding engine)

The old detached `/Users/aleksandrfeer/Projects/AIVERSE-batching-stage98` already contains a compiled fixture and may be used by fetching two script objects individually. Do not reset any worktree with an unresolved rebase.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage98 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage104-trials.sh \
  > tools/batching-stage104-trials-local.sh || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage104-analyze.py \
  > tools/batching-stage104-analyze.py || exit 1
bash -n tools/batching-stage104-trials-local.sh || exit 1
python3 -m py_compile tools/batching-stage104-analyze.py || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage98/aiverse_material_batches \
  bash tools/batching-stage104-trials-local.sh
```

At the end share the **new Stage104 `capture-*.tar.gz`**, or `stage104-summary.csv` plus offending log if an assertion fails. If the shell runner detects a native error, it exits nonzero immediately and leaves its unique capture directory intact. It does not alter other worktrees. For accurate comparison, avoid treating process execution order as causal randomization; GPU timestamps and variance under representative workloads remain the next stage. Do not merge PR #39 yet.

### Agent continuation

Read this handoff and parse native owner archive. Check the 8 per-process CSVs for 81 measured frames, real Direct/MDI mode and screenshot parity. Compare CPU median/P95/worst and per-pass backend across 2K/4K; inspect Mach CPU vs wall for potential contention. If 4K direct mode causes performance or memory pressure, bound the test and document. Only propose low-level batching optimization after seeing actual scaling and preserving correct Metal pipeline states, uniforms, and resource residency.


---

## Stage 105 — correct 2K/4K analysis false rejection of submitted commands (2026-11-20)

**Repository:** `chromealex/AIVERSE`, branch `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Change:** `5018b3810f81342b42aa9977ea4b046de08a4400` updates `tools/batching-stage104-analyze.py`; this handoff follows in its own commit. **No renderer, Metal, RHI or stress fixture C++ code changed.**

### Owner's actual Stage 104 Mac output / blocker

The owner ran the paired 2K/4K Stage104 test. The Direct vs MDI runner reached its analyzer successfully after executing its own run assertions and got:

```text
2048 direct1  wall=2.3557ms p95=2.4445ms mach=2.3410ms mdi_commands=0
ValueError: .../2048-mdi1-frames.csv: forced MDI expected 4096 indirect commands and nonzero submissions, got 4086/18
```

The fixture authored 2048 unique primitives but the renderer actually submitted **4086 indirect commands in 18 MDI submissions** for that measured 2K frame, rather than 4096 commands. The cause of this ten-command difference is **not yet independently established**; possible pass-specific visibility/culling/filtering is a hypothesis, and raster pixel parity/shape of the other benchmark logs must be used to rule out incorrect omission. The analyzer's unconditional exact equality `commands == 2*items` was not a legitimate invariant because submitted draws represent processed eligible/visible work, not all authored pass candidates. The traceback originates from the **analyzer after native process runs**, not C++/Metal failure. Actual Mac 4K values and completed aggregate report have not yet been provided.

### Fix

- For forced MDI, require `mdi_submissions>0`, `mdi_commands>0`, `mdi_commands<=2*items` on **all 81** measured frames.
- Reject anomalously low work coverage `mdi_commands < 90% * (2*items)` as a defensive heuristic, **not** a required engine invariant.
- Require MDI submission and command counts **remain constant for all 81 measured frames** of this fixed static stress scene (no silent fallback/change in MDI groups). Direct still requires strictly 0 MDI commands/submissions for every frame.
- Report median MDI submissions and actual commands, plus `authored_pass_commands=2*items` and `mdi_command_coverage_pct`. For the reported 2K case, `4086/4096 = 99.756% coverage`, with 18 actual MDI submissions. This count does not prove all visible geometry rendered, which is why byte-for-byte pixel parity must still be required by the runner.
- Preserve all existing CPU `renderer.render()` wall/Mach/backend analysis, median/P95/max and two Direct vs two MDI comparisons per workload size.

### Validation status

**Performed:** inspected existing fixture and Stage104 analyzer via GitHub; identified faulty exact-count condition; updated source, wrote this handoff. **Not performed:** new native Mac run or rerun of fixed analyzer on owner's real Stage104 capture folder (Mac-only); complete 4K benchmark results, independent CPU/GPU scaling parity, or explanation of the ten omitted commands. The existing Stage104 runner has already finished its fixture passes if there are eight `*.log` and `*-frames.csv` artifacts in that capture directory; do **not** rerun expensive rendering just to fix a Python assertion. CPU MDI/AUTO flags remain opt-in, PR #39 stays Draft.

### Owner: rerun only analyzer on existing output

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage98 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage104-analyze.py \
  > /tmp/aiverse-stage105-analyze.py || exit 1
python3 /tmp/aiverse-stage105-analyze.py \
  build/batching-stage98/stage104/capture-20261120T071923Z-97395
```

This will output all four runs per size and write `stage104-summary.csv` to the existing capture folder, if all eight CSVs were generated. It will not rebuild, rerender or modify Git state. If the analysis succeeds, share `stage104-summary.csv` and the existing capture archive or CSV/logs for detailed follow-up. If a new `ValueError` appears, preserve the exact message and inspect the corresponding file rather than suppressing correctness checks.

### Next stage

Interpret actual 2K/4K Direct vs MDI process medians and P95, compare Mach thread time and per-pass backend, ensure parity logs show zero changed channels in both forced modes. Check whether 4086 vs 4096 is explained by filtering or an unexpected missing batch; only then consider altering batching limits/thresholds. No GPU execution time/FPS measured.


---

## Stage 106 — owner's complete Stage 104 2K/4K native CPU Direct/MDI results (2026-11-20)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/auto-batch-cost-metrics-20261119`; **PR #39 remains Draft**. **Changes this stage:** handoff/documentation only, no renderer, CPU batching policy, Metal backend, benchmark fixture or analysis scripts touched. Previous analyzer fix: `5018b3810f81342b42aa9977ea4b046de08a4400` (Stage105).

### Actual Mac Studio owner test and evidence

The owner successfully ran eight Stage 104 native fixture processes (`2048/4096` authored unique primitives, Direct1/MDI1/MDI2/Direct2 order for each load). Each process printed `PASS` from the runner's assertions before the initial analyzer rejected valid `4086/18` MDI counts. Then the owner fetched the Stage105 revised Python analyzer from GitHub and executed it **against the already-created eight frame CSVs** in `build/batching-stage98/stage104/capture-20261120T071923Z-97395/`. Revised analyzer completed, printed all per-process CPU wall and Mach medians, and created `stage104-summary.csv`. No new native render test was required after the assertion fix.

Owner-supplied analyzer stdout (CPU synchronous `renderer.render()`, all ms):

```text
2048 direct1 wall=2.3557 p95=2.4445 mach=2.3410 mdi_submissions=0 mdi_commands=0
2048 mdi1    wall=1.5882 p95=1.7063 mach=1.5780 mdi_submissions=18 mdi_commands=4086 coverage=99.76%
2048 mdi2    wall=1.6063 p95=1.7189 mach=1.5930 mdi_submissions=18 mdi_commands=4086 coverage=99.76%
2048 direct2 wall=2.3506 p95=2.4334 mach=2.3400 mdi_submissions=0 mdi_commands=0
2048 paired process-median Direct=2.3531ms MDI=1.5972ms change=-32.12%
4096 direct1 wall=4.5474 p95=4.6783 mach=4.5350 mdi_submissions=0 mdi_commands=0
4096 mdi1    wall=2.9151 p95=3.0731 mach=2.8930 mdi_submissions=36 mdi_commands=8172 coverage=99.76%
4096 mdi2    wall=2.9801 p95=3.1285 mach=2.9640 mdi_submissions=36 mdi_commands=8172 coverage=99.76%
4096 direct2 wall=4.6848 p95=4.8395 mach=4.6510 mdi_submissions=0 mdi_commands=0
4096 paired process-median Direct=4.6161ms MDI=2.9476ms change=-36.14%
Saved: .../capture-20261120T071923Z-97395/stage104-summary.csv
```

For both loads, Direct/MDI process medians are internally fairly close and Mach thread CPU tracks wall, suggesting relatively little measured off-CPU during these particular runs. **Important:** the comparison is process-interleaved but not per-frame randomized, and remains CPU-only. Never translate these percentages to FPS or GPU speedup.

### Functional interpretation / remaining discrepancy

- CPU MDI lowers observed CPU `render()` medians by **32.12%** at 2K and **36.14%** at 4K vs direct median across two separate processes each.
- Both 2K MDI trials consistently use 18 submissions and 4086 commands vs upper bound 4096; both 4K MDI trials use 36 submissions and 8172 vs upper bound 8192; coverage is **99.75586%** in both. These counts are stable across all 81 sampled frames of each process (the updated analyzer requires it).
- The 10-command (2K) and 20-command (4K) differences scale exactly with workload size but their **source has not been established**. Potential visibility culling or eligibility filtering is a *hypothesis*, not proven. Inspect renderModels batching and frustum/shadow visibility counters plus image parity and/or CPU direct draw counts before accepting this difference as intentional.
- Stage104 runner requires the fixture's CPU invariant PASS, terminal image parity `changed_channels=0`, for MDI the *actual submitted MDI frame* raster parity `changed_channels=0`, and 81 measured MDI/Direct frames with no MDI in forced-Direct. The owner's run passed those runner assertions, but the full eight native logs/PPM images were not independently inspected in this turn; trust only what the reported runner PASS verifies.
- Previous separate 1K experiment yielded Direct 1.2233ms, MDI 0.7739ms (~36.7% lower CPU wall), but it came from different process conditions and is not directly matched with current 2K/4K values.

### Validation truth / next agent action

**Actually performed:** analyzed user-provided full Stage105 output; verified head of PR #39 through GitHub connector; recorded owner-observed native checks and paired CPU timings in this handoff. **Not performed:** own native Mac build/renderer run, independent inspect of eight Stage104 CSVs/logs, GPU timestamps, Vulkan/GLES parity, real content performance, 10K stress, proof of cause for the small MDI command delta, full CTest. Do not change AUTO threshold or default-enable MDI based only on these synthetic CPU results; keep Draft PR #39.

Next: obtain owner's `stage104-summary.csv` or `capture-20261120T071923Z-97395.tar.gz` and examine per-frame counters and parity. Specifically trace why 2K loses 10 and 4K loses 20 commands relative to 2*items (e.g., pass-specific visibility and grouping rules); verify model visibility, culling and direct raster coverage before treating the gap as normal. After correctness explanation, add opt-in GPU timing/Metal frame capture and representative scene tests. Avoid touching shared or rebasing worktrees; keep detailed handoffs after every change.
