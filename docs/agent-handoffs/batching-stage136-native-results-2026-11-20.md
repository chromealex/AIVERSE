# AIVERSE — Stage 136: Mac Studio Metal CPU MDI geometry scaling — measured native results

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Implementation branch:** `perf/batching-stage136-native-results-20261120`  
**Parent branch:** `perf/batching-stage134-native-acceptance-20261120`, Draft [PR #82](https://github.com/chromealex/AIVERSE/pull/82)  
**Tested source HEAD:** `204b2a046306b99e9f8829f4a64a6e64ad3ffc36`  
**Stage type:** owner-native archive inspection, independent reproduction, performance interpretation, and agent handoff. **No production C++, Metal, shader, RHI, Vulkan, GLES, AUTO or policy code changed.**

## Exact native capture and provenance

The owner provided the actual `capture-20261120T115651Z-43216.tar.gz` produced by `tools/batching-stage134-native-mac.sh` on the Mac Studio. The assistant inspected the supplied tar directly in Python without relying only on copied summary text. The SHA-256 of the original uploaded archive is:

`346b02651096adf35d2fb1a7f5aaf1d3b95fc605b2fa65e24e3946ba13a4c002`

Size **251,298 bytes**, **103 archive entries** (100 regular files plus 3 directories). No archive bytes were uploaded into GitHub; the raw evidence is the user-supplied chat attachment and can be matched using the SHA above.

`manifest.txt`: stage 129 capture, `result=PASS`, UTC start 20261120T115651Z, end 20261120T115727Z, `git_head=204b2a...`, 2048 original model primitives, N=1/N=4 tessellation, 16 independent processes, 18 MDI submissions / 4086 MDI indirect commands per measured MDI frame. `native-provenance.txt`: stage 134, identical full HEAD, macOS **15.6**, model **Mac15,14**, Apple clang **17.0.0**, CMake **3.27.8**, compiled executable SHA-256 `39d5d563cb80a55d561f392604fd9c5d1262839c2288baab4aad4618d91462d2`. `mesh-unit.log` has `material palette CPU checks=38 result=passed` and `mesh subdivision CPU checks=passed`.

## Independent integrity checks actually performed against the archive

- Read each of the **16 GPU timeline CSVs**: 66 rows each, all marked `valid=1`; exactly **1056 completed GPU timestamps**.
- Read each of the **16 CPU timeline CSVs**: 106 rows each, 81 measured steady frames each (`115..195`); **1296 measured CPU frames**. Recomputed individual CPU medians and GPU medians/P95 from raw rows, agreeing with published Stage127 and Stage129 reports.
- Every frame of every Direct case has `mdi_submissions=0, mdi_commands=0, shadow_draws=2048, opaque_draws=2048`. Every measured MDI frame has `mdi_submissions=18, mdi_commands=4086, shadow_draws=14, opaque_draws=14`. These are **actual** recorded topology values, not presumed settings.
- All 16 logs report 38 passed CPU invariants. Verified with SHA-256 that the binary PPM `-before.ppm` and `-after.ppm` are identical **within every run**, and all 8 MDI `-mdi-after.ppm` images are also identical to the before image in their run.
- Confirmed geometry-pool uploaded bytes N=1: **368640 vertex + 24576 index = 393216 bytes**; N=4: **5898240 vertex + 393216 index = 6291456 bytes**. Both are below the 16 MiB admission limit and both report `pooled=1`.
- **Cross-process nuance:** within each N, there are 2 distinct raster SHA-256 outputs, each differing from the other by exactly **one RGB byte with absolute channel difference 1**. This is not an intra-process parity failure, but indicates tiny run-to-run pixel variation. Do not claim all 16 images are mutually byte-identical.
- `stage133-integrity.csv`, Stage127/129/132 CSVs, manifest and provenance exist. Their declared results match the independently inspected raw timeline and raster files.

## Whole Metal GPU command-buffer timing (ms)

These are **median of two independent process medians**, NOT isolated GPU model-pass times, vertex ALU cost, shader instruction timing, or full-game FPS. Positive MDI GPU delta means MDI is slower.

| Subdivisions | Triangles per primitive | Inherited ICB pipeline | Direct GPU median | MDI GPU median | MDI GPU premium | MDI GPU relative | Direct CPU render median | MDI CPU render median | CPU wall change |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | OFF | 0.590729 | 0.813010 | +0.222281 | +37.628% | 2.720850 | 1.493270 | −45.118% |
| 1 | 1 | ON | 0.574365 | 0.805385 | +0.231020 | +40.222% | 2.763730 | 1.338915 | −51.554% |
| 4 | 16 | OFF | 1.662638 | 1.988240 | +0.325603 | +19.583% | 2.758875 | 1.592870 | −42.264% |
| 4 | 16 | ON | 1.689760 | 1.996925 | +0.307165 | +18.178% | 2.701020 | 1.358170 | −49.716% |

**Conclusion:** CPU MDI saves about **42–52% CPU render wall** in this 2048-primitive benchmark, but its full Metal GPU command-buffer median remains **18–40% slower** than matched Direct. CPU submissions and indirect command counts are not the same as GPU draw cost or overall frame time. It is still inappropriate to turn on production MDI/AUTO from these synthetic results.

## N1 → N4 scaling, process repetition and drift

| Pipeline | Direct GPU growth | MDI GPU growth | Additional MDI GPU premium | Individual repeat effects | Max same-cell repeat span | Stage132 label |
|---|---:|---:|---:|---:|---:|---|
| OFF | +1.071909 ms | +1.175230 ms | **+0.103321 ms** | +0.100606 / +0.106037 ms | 0.086685 ms | `directional_candidate` |
| ON | +1.115396 ms | +1.191540 ms | **+0.076144 ms** | +0.049105 / +0.103184 ms | 0.030730 ms | `directional_candidate` |

Both repetitions for both inherited modes indicate a greater *absolute* MDI GPU premium at N=4 than N=1. The **relative percentage** shrinks because both paths get substantially slower with geometry work. Only two independent process medians per cell were collected; the Stage132 verdict is a descriptive guard, **not statistical significance or proof of the GPU ICB root cause**. Geometry density changes triangle processing, vertex fetch, rasterization and potentially fragments, not isolated vertex ALU.

Individual process GPU P95 outliers exist: e.g., N4 OFF MDI #2 P95 = 2.499500 ms versus its median 1.974915 ms; N1 ON Direct #2 P95 = 1.004250 ms vs median 0.585417 ms. Repeated independent trials and realistic-content controls are needed before making GPU backend decisions.

## What inherited pipeline demonstrates (and does not)

- N1 OFF MDI 0.813010 ms vs ON MDI 0.805385 ms (difference −0.007625 ms).
- N4 OFF MDI 1.988240 ms vs ON MDI 1.996925 ms (difference **+0.008685 ms**).
- Matched Direct controls also fluctuate between OFF and ON. Hence this new experiment **does not confirm a repeatable inherited-pipeline GPU benefit** at both geometry levels, even though Stage127 showed a small improvement in an earlier session.

The known AGX driver fault with inherited **buffers** makes restoring buffer inheritance an unsafe speculative shortcut; retain explicit per-command buffers, residency declarations and frame lifetime/fences. Keep pipeline inheritance default OFF until it produces reliable gains across representative hardware.

## Next engineering step and acceptance criteria

1. Treat the remaining CPU MDI GPU regression as **real on this exact fixture** but mechanism **unproven**. Do not tune global batching admission based solely on CPU reductions. The GPU premium grows in absolute milliseconds as geometry density increases at fixed MDI command/submission counts, suggesting more than a purely fixed setup cost; this is a hypothesis, not a causal decomposition.
2. Design a further controlled Metal-only opt-in experiment that isolates **ICB playback/state overhead** from geometry/fragment workload while holding actual draw topology, shader/pipeline state, viewport, and pixel parity fixed. Prefer matched Direct controls and repeated independent processes, and a supported GPU profiling path if one becomes available. Do not remove AGX safety workarounds.
3. Repeat with textured, mixed-material, transparent, skinned and LOD content on real scenes; determine GPU+CPU frame budget and total FPS before considering production AUTO policy changes. Vulkan/GLES paths remain required for eventual functional validation but were **not tested** by this capture.
4. If any subsequent change is made, create a separate handoff with exact branch/HEAD, changed files, real commands/tests and next action. Source basis for this data-only stage is the original tested SHA `204b2a046306b99e9f8829f4a64a6e64ad3ffc36` and the 2026-11-20 owner archive above.

## Not performed during Stage 136

No new assistant-side native build, no re-run of the benchmark on Apple hardware, no new GPU command buffer timestamps or driver profiling, no full CTest, no new Vulkan/GLES compilation or mobile test. All native numbers come from the supplied Mac Studio archive and were independently recalculated in Python. This stage does not modify the GPU renderer or enable experimental features.
