# AIVERSE — Stage 138: verified owner-native raster-coverage Metal GPU results

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/batching-stage138-native-raster-results-20261120`  
**Parent branch / PR:** `perf/batching-stage137-raster-coverage-20261120`, [Draft PR #87](https://github.com/chromealex/AIVERSE/pull/87). This stage is documentation-only; production Metal, Vulkan/GLES, renderer, shaders and policy are unchanged.  
**Exact tested source HEAD:** `2734cf47e5fa32a188cecf1eb928dba03174ec96`.  
**Raw user-provided evidence:** `capture-20261120T123130Z-56024.tar.gz`; SHA-256 `ef541af5c0bfaf51072b7aa7bbecde833c4446c269950fb1ede9c9ef2a58242c`.

## What was run and verified

User ran `tools/batching-stage137-native-mac.sh` on the native Mac Studio and submitted the raw archive. The archive's `manifest.txt` says `stage=137`, `result=PASS`, UTC start `20261120T123130Z`, end `20261120T123210Z`. `native-provenance.txt` reports macOS **15.6**, model **Mac15,14**, Apple clang **17.0.0**, CMake **3.27.8**, identical tested source SHA and compiled `aiverse_material_batches` binary SHA-256 `3b800dd706cf8578fb709a67534e53dccd4ef757dc553f1d004af26fbb696fb0`.

Assistant independently read **all archive contents directly from the supplied .tar.gz** using Python `tarfile`, not merely the summary CSV. The file holds 99 archive entries (96 regular files). Validation:

- **16/16 distinct independent native processes**, matrix triangle scale 1/4 × pipeline inheritance OFF/ON × Direct/MDI × repeat 1/2.
- **1,056 valid GPU samples:** 66 completed whole-Metal-command-buffer timestamps per process, strictly frames 115–180, each `valid=1`.
- **1,296 measured CPU frames:** each process has 106 timeline rows for frames 90–195, with 81 post-warm-up measured frames (115–195).
- **All 16 logs:** 38 CPU invariants passed, correct scale, N=4 / 16 triangles per original primitive, and `pooled=1 vertex_bytes=5898240 index_bytes=393216`.
- Actual Direct measured frames: **0 MDI submissions, 0 indirect commands, 2048 shadow draws + 2048 opaque draws**. Actual MDI measured frames: **18 ICB submissions, 4,086 indirect commands, 14 shadow groups + 14 opaque groups**. Verified on **every** measured frame, not just from log counters.
- All 16 `-before.ppm` vs `-after.ppm` raster images are **byte-identical** within each run, and all 8 actual forced MDI `-mdi-after.ppm` are identical to their corresponding before images. Each PPM has the correct P6 header and 960×640 RGB payload.
- Across scale1 vs scale4 of the first OFF/Direct control, exactly **1,275 of 614,400 RGB pixels differ** (about **0.2075% of viewport pixels**). Scale changes the visible raster as intended, but only a very small part of the viewport; do NOT infer 16× full-frame fragment processing or isolated fragment shader ALU cost.
- Within each scale the eight processes have two different RGB outputs, and those differ by **exactly one RGB byte of intensity 1 in one pixel**. There is no *intra-process* parity failure; do not claim all cross-process images are identical.
- Recomputed 16 process GPU median/P95 and CPU median metrics from the raw timeline rows and reconciled every row with `stage127-runs.csv`, then confirmed `stage127-comparison.csv` / `stage137-coverage-comparison.csv` aggregates. No unseen GPU measurements were inferred.

## GPU / CPU performance measurements

All times in milliseconds. GPU time is **whole completed Metal command-buffer duration**, not the isolated model pass, vertex stage or overall application frame/FPS. Values are medians of two independent process medians per cell.

| Triangle scale | Pipeline inherited | Direct GPU median | MDI GPU median | MDI GPU premium | MDI GPU change | Direct CPU median | MDI CPU median | MDI CPU change |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1× | OFF | 1.6839275 | 2.0408975 | +0.3569700 | +21.1987% | 2.773835 | 1.557915 | −43.8353% |
| 1× | ON | 1.6392725 | 1.9790950 | +0.3398225 | +20.7301% | 2.805230 | 1.362940 | −51.4143% |
| 4× | OFF | 2.0091650 | 2.4083450 | +0.3991800 | +19.8680% | 2.865790 | 1.614665 | −43.6572% |
| 4× | ON | 2.0099275 | 2.4046875 | +0.3947600 | +19.6405% | 2.740225 | 1.332585 | −51.3695% |

**Result:** forced CPU MDI continues to save around **44–51% CPU render wall time**, but loses **19.6–21.2% on whole-GPU-CB median** to Direct even when raster coverage changes at identical vertex/index count and GPU draw topology. The absolute MDI GPU premium remains around **0.34–0.40ms**, not a demonstrated GPU win.

The medians suggest `scale4 - scale1` premium growth of **+0.042210ms** with inherited pipeline OFF, and **+0.0549375ms** with inherited pipeline ON. That alone is **not reliable evidence** of a coverage-dependent overhead because paired process results are inconsistent.

## Paired run results and lack of stable coverage interaction

| Pipeline | Repeat | Small MDI-minus-Direct GPU premium | Large MDI-minus-Direct premium | Large minus small |
|---|---:|---:|---:|---:|
| OFF | 1 | +0.360355 | +0.324650 | **−0.035705** |
| OFF | 2 | +0.353585 | +0.473710 | **+0.120125** |
| ON | 1 | +0.380020 | +0.390630 | **+0.010610** |
| ON | 2 | +0.299625 | +0.398890 | **+0.099265** |

**OFF changes sign between repeats**, and ON changes by nearly an order of magnitude across repeats. Process scheduling/thermal/driver variance and scene-boundary effects are not separately controlled, and there are only two processes per cell. Therefore do **not** claim the MDI premium rises reliably with raster coverage, or that a particular GPU driver stage has been identified.

Measured GPU P95 values vary noticeably. Example: scale4 OFF Direct #1 P95=2.52317ms versus its median 2.061205ms; OFF MDI #2 P95=2.75013ms versus median 2.430835ms. The instrument measures completed CB GPU timing and is NOT a per-pass or real-game FPS metric.

## Impact on Stage136 and production policy

Stage136's original N1-to-N4 experiment increased vertex and triangle count at fixed MDI commands, with a consistent positive absolute GPU MDI premium delta, but was not a pure raster test. Stage137 fixes the geometry and indices, changes only triangle footprint, and does not confirm a reproducible *additional* MDI premium attributable to changed coverage. **The reliable shared finding** is that MDI has a considerable GPU penalty relative to Direct on this synthetic fixed-2048-primitive scenario despite pronounced CPU savings.

The CPU Metal ICB path explicitly binds per-command vertex and fragment buffers, argument buffers and resource residency. AGX G15 previously faulted on inherited buffer state, so **do not restore buffer inheritance**, disable residency/fences, or enable MDI/AUTO in production to chase a speculative win. The opt-in inherited pipeline mode is not a reliably proven across-content fix.

## Changes / tests / next handoff

**Changes in Stage138:** this `docs/agent-handoffs/batching-stage138-native-raster-results-2026-11-20.md` only. No renderer, shader, GPU/Metal, test fixture, RHI, Vulkan, GLES or production default changes.

**Actually performed:** original archive SHA-256 and provenance; 16 real per-process log/CSV/PPM inspections; every measured frame's topology, 1,056 valid GPU timestamps, 1,296 measured CPU frames, 16 intra-run raster parity comparisons and 8 extra MDI capture comparisons; per-process medians/P95 calculated directly; 1,275 altered pixels across geometry footprint scales; cross-process one-byte raster differences; aggregation/pair-repeat interpretation.

**Not performed:** new assistant-run native Mac compilation, fresh GPU tests, GPU shader/encoder stage counter profiling, experiment with realistic textured/mixed-material/animated/transparent content, Vulkan/GLES builds, full CTest, statistically powered repeated experiment, or a Metal backend optimization. The archive is user-owned evidence, not published to GitHub as raw binary bytes.

**Next agent:**
1. Check CI on the exact Stage137 and Stage138 commit heads. If queued, do not claim PASS.
2. Reproduce key metrics from raw CSV if needed; retain raw archive hash and compiler/hardware provenance.
3. Instead of drawing a fragment-cost conclusion, investigate ICB **per-command execution and resource binding** versus matching Direct at constant geometry. If new diagnostic code is warranted, use *opt-in, reversible, single-variable* experiments retaining explicit per-command buffer bindings and AGX crash mitigations. A controlled alternative is to compare shadow-only / opaque-only pipelines at fixed N4 geometry and both scales, with actual GPU timing, raster parity and Direct controls; avoid conflating full-CB timing with per-pass GPU timings.
4. Test representative real scenes and cross-backend integration before changing Metal AUTO admission/defaults.
5. Add the next **separate agent handoff** describing actual modified files, architecture/API, exact branch/full SHA, tests actually run, unresolved constraints and next concrete step.
