# AIVERSE — Stage 133: independent Metal CPU MDI capture integrity

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage133-archive-verification-20261120`  
**Draft PR:** [#79](https://github.com/chromealex/AIVERSE/pull/79) targeting `perf/batching-stage132-run-reliability-20261120` (Draft PR #78); the earlier chain is Draft PRs #77 → #75 → #39. Do not merge directly to main.  
**Pinned source base:** `01ed5bbba2b874487fd4a618ea050f00736c2740`.  
**Changes before handoff:** `e0fdfdcb12c408de6b80cddf40440f0b0cc38f68` (verification utility), `3622ffdc6684443d2335cf0e5ec9bf21d87c5927` (native runner gate), `c02f694d822f45838f1500f2f62c7d94481bc094` (CI self-test).

## Purpose / baseline

Stage131 corrected the N1/N8 GPU benchmark to N1/N4 to remain below the per-model 16 MiB Geometry Pool admission ceiling. Stage132 added repeated Direct/MDI GPU-premium diagnostics. Both still require a **native Mac Studio 16-process Metal capture**; neither delivered new verified Metal GPU timings when Stage133 began. Previous Stage127/128 synthetic findings (CPU MDI wins CPU wall time but loses whole-CB GPU time) remain only a diagnostic baseline, not the cause.

An incomplete or mismatched archive could appear superficially successful if the separate per-process assertions were satisfied earlier, or if a stale CSV or absent raster image were distributed. Stage133 is an independent **file-integrity acceptance gate** before the runner writes `result=PASS`.

## Implemented and contract

1. `tools/batching-stage133-verify.py` (stdlib Python3): reads `manifest.txt`, demands Stage129 inventory (two mesh levels N1/N4, 16 processes, fixed 18/4086 ICB topology, `geometry_pool_required=1`, 40-character hexadecimal git SHA), rejects `result=FAILED`.
2. For every 2048-{off,on}-{direct,mdi}{1,2} entry in each `mesh1/` and `mesh4/` folder:
   - Check presence of run log, nonempty CPU/GPU frame CSV files, per-mesh Stage127 run and comparison reports, all Stage129/132 aggregate reports.
   - Check actual pooled geometry log, positive vertex/index byte sizes, and combined size at most 16 MiB; compare expected N/N² triangle-geometry label.
   - Check CPU invariant pass, 66 claimed GPU samples, 106 claimed timeline rows, actual Direct/MDI mode-specific run counters, final `changed_channels=0` and actual MDI parity when MDI is forced.
   - Stream-hash `-before.ppm` and `-after.ppm` and (when MDI) `-mdi-after.ppm`; demand exact `P6\n960 640\n255\n` header, 960×640×3 RGB bytes and identical SHA-256. Streaming limits peak memory overhead. This is stronger file-level detection of missing/corrupted captures, but it does not replace the frame timing/command topology checks in Stage127.
3. Write `stage133-integrity.csv`, one record per process, with mesh, pipeline, mode, repeat, actual pool allocation sizes and raster SHA-256.
4. Synthetic `--self-test` builds a full 16-case miniature capture with 1×1 PPM, verifies success, corrupts one MDI image byte and requires a hard error.
5. `tools/batching-stage129-trials.sh`: call Stage133 after Stage127/129/132 analyzers and before appending `result=PASS`. Existing ERR trap archives partial output.
6. `.github/workflows/gpu-batching.yml`: in existing macOS `metal-cpu-mdi` job, add `python3 -m py_compile tools/batching-stage133-verify.py` and `python3 tools/batching-stage133-verify.py --self-test`. The native macOS CI job **does not** become a Mac Studio performance benchmark.

No production renderer, Metal, Vulkan, GLES, shader, RHI, AUTO/MDI defaults, GPU resource residency, ICB buffer binding or frame-fence modifications.

## Actual tests and limits

**Actually performed:** retrieved Stage132 source and Stage129 runner from GitHub; inspected frame PPM emission in `tests/render/material_batches.cpp` (before, after, optional frame-100 MDI image), modeled the file contract, and **executed a local copy of the checked-in Stage133 Python script** using `python3 -m py_compile` and `python3 ... --self-test`. The local test output was: `PASS: Stage133 archive audit self-test and raster corruption rejection`. The committed GitHub tool file has the same 9627-character source as the locally tested script. Source anchor checks for CI and native runner integration were also performed.

**Not performed:** actual GitHub-hosted Python self-test or code compilation on AppleClang, native Metal 16-run capture, real PPM file validation, real runtime Geometry Pool admission, new GPU timestamps, full CTest or Vulkan/GLES tests. CI was queued for Stage132 when inspected and may be queued for this commit. A synthetic 1×1 PPM **does not prove** native raster parity, nor does matching PPM across frames validate performance or cross-backend portability. Keep Draft until real results arrive.

## Native Mac Studio acceptance

Use a fresh detached worktree of the Stage133 branch; **do not** reset or modify ongoing TerraWeave/Stage127/129/131/132 worktrees or archives:

```bash
ROOT="$HOME/Projects/AIVERSE"
WT="$HOME/Projects/AIVERSE-batching-stage133"
git -C "$ROOT" fetch origin perf/batching-stage133-archive-verification-20261120 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/batching-stage133-archive-verification-20261120 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage129" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage129" \
  --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
bash -n tools/batching-stage129-trials.sh || exit 1
python3 -m py_compile tools/batching-stage127-analyze.py \
  tools/batching-stage129-analyze.py \
  tools/batching-stage132-analyze.py \
  tools/batching-stage133-verify.py || exit 1
python3 tools/batching-stage129-analyze.py --self-test || exit 1
python3 tools/batching-stage132-analyze.py --self-test || exit 1
python3 tools/batching-stage133-verify.py --self-test || exit 1
build/batching-stage129/aiverse_material_batches --unit || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage129/aiverse_material_batches \
  bash tools/batching-stage129-trials.sh
```

On native success, the `Stage129 archive: ...tar.gz` contains the new `stage133-integrity.csv` plus all Stage127/129/132 reports. Required acceptance targets, not yet observed: 16/16 processes, 1056 valid GPU CB timestamps, 1296 measured CPU frames, 16 pooled-geometry admissions, no missing images, unchanged raster output in each mode, actual MDI 18 submissions and 4086 commands/measurement frame.

To independently recheck an extracted native archive: `python3 tools/batching-stage133-verify.py PATH_TO_CAPTURE_DIRECTORY`. This checks image/log integrity; still run Stage127/132 timing analysers for numerical interpretation.

## Next agent handoff

1. Check GitHub Actions status for **exact final Stage133 HEAD**. Record check statuses and exact first failing steps; don't claim PASS while queued.
2. Run the commands on Mac Studio or use user-submitted archive; verify checksums, then compare Stage132 repeated GPU premiums and drift budget. Only then decide whether another diagnostic or safe backend optimization is justified.
3. If native N4 pool rejection, crash, or pixel mismatch occurs, preserve failure archive and investigate. Do not weaken AGX crash safeguards, resource residency, or Direct fallback just to pass.
4. Keep every stage genre-independent and respect Metal/Vulkan/GLES common RHI constraints; never enable MDI/AUTO by default from synthetic benchmarks.
5. Commit a **new separate handoff** after any next step, with branch+full SHA, API/files, actual tests, results, limitations and exact next action.
