# AIVERSE — Stage 132: paired Metal MDI reliability audit

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage132-run-reliability-20261120`  
**Draft PR:** [#78](https://github.com/chromealex/AIVERSE/pull/78), base `perf/batching-stage131-pooled-geometry-20261120` (Draft PR #77). The chain continues through Draft PRs #75 and #39; none of this stage targets `main` directly.  
**Exact starting SHA:** `b053f4eec228eb3db48cdccdb7273f77e56f9972` (Stage131).  
**Implemented commits before this handoff:** `41bd74cc0d15c4b989c88eb778b8b0b2b43fd517` (analyzer); `56155965cd696d7d8629633f0f9729f506a62b3e` (runner); `c7160599e10024a111f5b8c62b31406b79f3e022` (CI).

## Problem to solve

Stage127–128 Mac Studio trials showed inherited Metal ICB pipeline state reduces synthetic CPU MDI whole-GPU-CB time, but forced CPU MDI remained ~37% slower than Direct on GPU. Stages129–131 created a cleaner fixed-command test with N1/N4 tessellation and guaranteed shared Geometry Pool allocation, keeping 2048 model primitives and (forced MDI) 18 ICB submissions + 4086 commands per measured frame.

Stage129's original `stage129-summary.csv` uses the median of two process medians for each Direct/MDI control. With only two processes per cell, it can hide inconsistent per-repeat effect signs, baseline drift and high-P95 tails. No Stage131 owner Mac Studio timings were submitted when this stage was written. A reported `MDI premium` or `scaling delta` is not, by itself, proven optimization headroom or evidence of vertex ALU cost.

## Implemented

**`tools/batching-stage132-analyze.py`** is Python 3 stdlib-only:

- Reads `mesh1/stage127-runs.csv` and `mesh4/stage127-runs.csv` (eight distinct process rows each: inherited pipeline OFF/ON x Direct/MDI x repeat 1/2).
- Rejects duplicate or missing process controls, invalid numeric/P95 ordering, non-finite or nonpositive CPU/GPU timings, incorrect 66-GPU-sample counts, and draw topology different from Direct (0 MDI submissions/commands, 2048+2048 model draws) or MDI (18 submissions, 4086 commands, 14+14 model groups).
- Independently reconciles the per-process rows with each mesh's `stage127-comparison.csv` aggregates (GPU Direct, MDI, premium; CPU Direct and MDI). A stale or mismatched summary aborts.
- Computes repeat-paired MDI-minus-Direct GPU premiums at N1 and N4, and their N4-minus-N1 difference for each inherited pipeline OFF/ON.
- Reports the absolute process-1 versus process-2 span for Direct and MDI at both geometry workloads and uses the **largest observed span** as a descriptive drift budget.
- Emits `stage132-repetitions.csv` with both repetitions' GPU medians, GPU P95, CPU medians and premium scaling, and `stage132-reliability.csv` with effects, observed spans and a descriptive verdict.
- `directional_candidate` only if both repeat-paired effects have the same nonzero sign **and** each absolute effect exceeds the observed maximum process span. Otherwise `inconclusive`. This is intentionally conservative and is **NOT statistical significance**, independent variance estimation, or grounds to enable MDI in production.
- Includes `--self-test` for synthetic positive-signal observations, deliberate command-topology corruption, and high-drift inconclusive observations.

**`tools/batching-stage129-trials.sh`** invokes the Stage132 analyzer after existing Stage127-per-mesh and Stage129 aggregation and *before* writing `result=PASS` to the manifest; a bad comparison therefore fails the capture and preserves failure archives.

**`.github/workflows/gpu-batching.yml`** extends the existing macOS `metal-cpu-mdi` offline gate with Stage132 `py_compile` and `--self-test`. Existing Metal raster parity checks remain untouched. The CI job is not a Mac Studio performance benchmark.

No production C++/Objective-C++/Metal, Vulkan, GLES, shader, render policy, AGX crash mitigation or compiled fixture changes in Stage132.

## Validation scope

**Actually done:** retrieved and reviewed source of Stage127 analyzer, Stage129 runner and Stage131 geometry validation from GitHub; created an isolated branch, authored scripts and CI changes; used GitHub compare to verify exactly the new script, the one-line runner integration, and CI additions. Source-level reasoning checked the 16-process control grid, paired-process logic, and Stage127 CSV column contracts.

**NOT done:** executed the checked-in Python script with actual `python3` or its `--self-test`, compiled the engine, executed `bash -n` on the checked-in runner, run the 16 Metal GPU trial processes, or collected raster parity / GPU measurements. GitHub CI may execute those offline checks later; check its conclusion for the latest HEAD before claiming PASS. No game-scene FPS, GPU model-pass timestamps or macOS hardware provenance is claimed.

## Mac Studio instructions

Create a **fresh detached** worktree from Stage132, without resetting the user's other worktrees or archives. The Stage132 runner retains the Stage129 name and writes the two extra Stage132 reports.

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage132"
git -C "$ROOT" fetch origin perf/batching-stage132-run-reliability-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/batching-stage132-run-reliability-20261120 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage129" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage129" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage129-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py \
  tools/batching-stage129-analyze.py \
  tools/batching-stage132-analyze.py || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
python3 tools/batching-stage132-analyze.py --self-test || exit 1
build/batching-stage129/aiverse_material_batches --unit || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage129/aiverse_material_batches \
  bash tools/batching-stage129-trials.sh
```

The `Stage129 archive:` printed on success should include the two new Stage132 CSVs in the root; all existing Stage127/129 files, raster captures and provenance remain. Accept only 16/16 real GPU processes, pooled geometry on both N variants, byte parity and correct topology, 1056 valid completed Metal GPU samples and 1296 measured CPU frames, followed by successful Stage132 analyzer execution. These are **targets** pending Mac Studio validation.

## Next agent

1. Inspect newest GitHub CI for PR #78; record exact HEAD/check names/result, fix any real failures without weakening validation.
2. Run `--self-test` and then the complete 16-case native Metal A/B; check all logs and archive manifests. If GPU/ICB path crashes or a pooled-geometry control fails, stop and retain diagnostic archive.
3. Analyze `stage132-repetitions.csv` and `stage132-reliability.csv`: compare all per-repeat premiums, P95 and process spans, not only median-of-medians. If `inconclusive`, collect more independent runs before making code changes.
4. Only once a reproducible GPU regression mechanism is supported by representative content, optimize CPU Metal ICB while preserving explicit per-command buffer slots, resource residency, fences and AGX safety. Cross-backend functional correctness remains essential; none of these Stage132 metrics justifies enabling production MDI/AUTO.
5. Record next stage in a **new agent handoff**: actual tests, numerical results, changed files, exact branch/commit, limitations and next action.
