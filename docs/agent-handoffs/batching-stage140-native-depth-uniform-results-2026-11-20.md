# AIVERSE — Stage 140: native Metal fragmentless-depth ICB binding A/B results

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Documentation branch:** `perf/batching-stage140-native-depth-uniform-results-20261120`  
**Tested source HEAD:** `1c3b6085e915e19fcbc648f3490565e602538884`  
**Parent:** `perf/batching-stage139-depth-fragment-icb-20261120`, Draft [PR #90](https://github.com/chromealex/AIVERSE/pull/90). This is a **documentation-only** stage.  
**User-provided Mac capture:** `capture-20261120T130816Z-80382.tar.gz`  
**SHA-256 of original uploaded tar.gz:** `a09770e8dc74c725aba8815d3a8a2200393eacca3dd242ac7df9d78ecee64897`  
**Compressed archive size:** 198,764 bytes, 72 tar entries (71 regular files and one folder). The original archive is user-supplied evidence and is **not** uploaded to GitHub.

## Purpose / prior evidence

Stage138 independently validated 16/16 owner-native Metal processes: CPU MDI wins CPU render wall (~44–51%) but loses ~19.6–21.2% in whole completed GPU command-buffer median at fixed 2048 authored model primitives. Its raster-coverage experiment did not establish a repeatable coverage-dependent GPU premium. Stage139 tests one reversible idea: under a genuinely fragmentless depth-only Metal ICB pipeline, skip per-command fragment uniform buffer bindings that have no fragment consumer, while keeping **all vertex bindings, uniform snapshots, residency, GPU fences, buffer descriptor safety and Direct fallback**.

Stage139's experimental flag, `AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS=1`, is OFF by default. OFF control uses literal `0`; pipeline inheritance is forced OFF in both cases to avoid confounding.

## Native hardware, exact code and capture integrity

`manifest.txt`: stage139, UTC `20261120T130816Z` to `20261120T130851Z`, `result=PASS`, 12 independent processes, exact tested SHA above, N4/triangle scale4, geometry pool required, inherited pipeline disabled, captured actual skip ON/OFF and Direct/MDI.

`native-provenance.txt`: macOS **15.6**, Mac model **Mac15,14**, Apple clang **17.0.0 (clang-1700.6.4.2)**, CMake **3.27.8**, compiled `aiverse_material_batches` executable SHA-256 **`484b3cf40199cd71bf206a818a3ec1a211ac7eca613bfb1a08575227b2e824b9`** and matching source HEAD.

Independently verified with the uploaded tar file using Python's standard library `tarfile`, `csv`, `statistics`, `math`, `hashlib` and `re`:

- **12/12 real native processes** (skip OFF/ON × Direct/MDI × 3 repeats), no missing logs, GPU CSV, CPU CSV or PPM files.
- **792 valid completed whole-Metal-GPU-command-buffer samples**, exactly 66 per process with frame IDs 115–180 and `valid=1`; **972 measured CPU frames**, 81 per process (from 106 timeline frames 90–195, measured 115–195).
- All processes passed 38 CPU invariants, model N4 and triangle scale4, `pooled=1 vertex_bytes=5898240 index_bytes=393216` (total 6,291,456 bytes), full shadow+opaque pass and group cap0.
- On **every** measured Direct frame, `mdi_submissions=0`, `mdi_commands=0`, `shadow_draws=2048`, `opaque_draws=2048`. On **every** measured MDI frame, `mdi_submissions=18`, `mdi_commands=4086`, `shadow_draws=14`, `opaque_draws=14`. Six MDI processes report `[metal-icb-depth-uniforms] mode=off|on eligible=1 slots=4`; all Direct controls have no such marker. The real optimized branch was exercised; this is not a no-op experiment.
- Verified **12/12 before/after PPM pairs are binary-identical**, and six additional actual MDI frame-100 `-mdi-after.ppm` images match their own before images byte-for-byte. All PPM are P6 960×640, 1,843,215 bytes including header. There are two cross-process raster SHA values: 10 processes have one, two Direct processes have the other; the images differ by exactly **one RGB channel byte, 149 vs 150** at a single position. The tiny cross-process difference does not undermine *within-process* pixel parity, but do not claim all images across all processes are byte-identical.
- Recalculated all 12 individual GPU medians/P95/max and CPU medians/P95 from raw frames, cross-checked to `stage139-runs.csv`, then independently recomputed all three paired effects and both summary rows to match `stage139-paired.csv` and `stage139-summary.csv`.

## Timing results — GPU full command buffer, milliseconds

GPU values are median of **three independent process medians**, NOT isolated model/depth-pass timings or full application frame/FPS. Positive GPU MDI premium means MDI is slower than matched Direct.

| Skip fragmentless depth fragment-uniform bindings | Direct GPU median | MDI GPU median | MDI GPU premium | MDI GPU change | Direct CPU render median | MDI CPU render median | CPU render change |
|---|---:|---:|---:|---:|---:|---:|---:|
| OFF (`0`) | 2.032000 | 2.441665 | **+0.409665** | +20.1607% | 2.962710 | 1.627710 | −45.0601% |
| ON (`1`) | 1.999645 | 2.490895 | **+0.491250** | +24.5669% | 2.959040 | 1.515040 | −48.7996% |

The **aggregate MDI GPU premium grew by 0.081585 ms** (around +19.9% of the OFF premium), rather than shrinking. Raw MDI GPU median was +0.049230ms higher ON than OFF, while the Direct control decreased by 0.032355ms. The matched-control premium is the relevant comparison; ON does **not** demonstrate a GPU win.

Individual matched-control repeats (positive = **ON is better**):

| Repeat | OFF MDI GPU premium | ON MDI GPU premium | OFF minus ON improvement |
|---:|---:|---:|---:|
| 1 | +0.415415 | +0.498625 | **−0.083210** |
| 2 | +0.391230 | +0.397915 | **−0.006685** |
| 3 | +0.439480 | +0.523065 | **−0.083585** |

Every paired result is negative, but one effect is essentially zero; with only three independent repetitions, this is descriptive evidence, not a statistically powered demonstration of a slowdown or its cause.

The ON MDI CPU render median, 1.515040 ms, is lower than OFF, 1.627710 ms (~6.92% lower). This could reflect reduced CPU encoding work, process drift or both. It does **not** establish a net application FPS benefit, and cannot justify ON with worse matched GPU timings on this device. MDI vs Direct CPU render wall savings remain strong in both modes.

GPU tails show outliers: ON MDI #3 maximum whole-GPU-CB = **11.10220 ms** (median 2.52271 ms), ON Direct #2 max = 4.00813 ms. P95 for ON MDI #3 = 2.82129 ms. Use the 66-sample median and repeat-level controls, not a cherry-picked extreme to explain the cost.

## Engineering decision

**Reject enabling the new fragmentless-depth skip in production.** Keep `AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS` OFF/unset; the default Metal descriptor, explicit vertex/fragment bindings, `inheritBuffers=NO`, resource residency, uniform snapshot GPU lifetime, AGX safeguards and Direct fallback must remain unchanged. The Stage139 code can stay isolated in Draft PR #90 as reproducible experimental evidence, or the experimental branch may later be closed/reverted after review. Do **not** merge this speculative optimization into the production code or enable CPU MDI/AUTO globally from this result.

The skip visibly exercised the intended eligible path but did not resolve MDI's GPU regression. It does **not** prove that fragment buffer bindings increase GPU time. Whole-CB GPU measurement cannot isolate ICB playback, draw dispatch, vertex/fragment ALU or state residency; more precise platform profiling or a different controlled experiment is required.

## Files changed in Stage140, actual tests and limitations

**Changed:** only this separate `docs/agent-handoffs/batching-stage140-native-depth-uniform-results-2026-11-20.md`. No C++, Objective-C++, RHI, shaders, Metal, Vulkan, GLES, renderer policy or test harness changes in this stage.

**Actually performed:** independent Python inspection of 71 real files from the user-provided tar.gz; cryptographic SHA-256 of archive and PPM images; all 12 real GPU/CPU frame timeline reads and per-frame topology checks; all native log/instrumented-path assertions; 12 primary plus six extra pixel parity checks; recomputation of medians, P95, outliers, three matched-control premium effects and both summary rows; GitHub verification that tested commit is the exact Draft PR #90 HEAD when inspected. **No assistant-run native hardware benchmark was added**; source of native numbers is the user's Mac Studio archive.

**Not performed:** new Mac native compilation, full CTest, Vulkan/GLES builds, statistically powered repetition, GPU-only depth-pass counters, alternate devices, or real content FPS tests. GitHub Actions for tested Stage139 HEAD were still queued when checked, so no CI PASS is claimed.

## Next agent and immediate next step

1. Track CI for exact PR #90 SHA and treat unverified/uncompleted checks accurately. Keep opt-in feature disabled.
2. Instead of tweaking the same fragmentless uniform binding, investigate **per-command indexed ICB playback versus equally indexed Direct draws**, with constant materials, shader/pipeline, geometry, and viewport. Prioritize controlled experiments that compare Metal indirect-command execution characteristics while preserving pixel parity; if hardware profiling is unavailable, use independent process repetition and paired Direct controls to avoid explaining driver internals from CB timestamps alone.
3. Reassess benchmark decision under representative textured/static/mixed-material/LOD/transparent/animated scenes, plus CPU, GPU and editor full-frame cost. MDI may make sense only when combined frame-budget savings are proven, not simply draw-call reduction.
4. Retain `inheritBuffers=NO`, all explicit vertex/PBR buffer binding and `declareIndirectRead` residency/fences; never disable AGX crash protections to chase an unsupported speedup. Vulkan/GLES functional paths must remain unaffected.
5. After the next implementation stage, add an independent new handoff with branch/commit, modified APIs/files, **actual** tests/results, limitations and concrete follow-up.
