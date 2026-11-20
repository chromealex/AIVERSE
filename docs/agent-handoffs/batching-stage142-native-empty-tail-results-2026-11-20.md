# AIVERSE — Stage 142: verified native Metal ICB empty-tail playback A/B

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Documentation branch:** `perf/batching-stage142-native-empty-tail-results-20261120`  
**Parent branch / PR:** `perf/batching-stage141-empty-icb-tail-20261120`, [Draft PR #93](https://github.com/chromealex/AIVERSE/pull/93), based on the Stage140 results branch.  
**Exact tested source HEAD:** `7bfc74a0b490fc19e07b918729a113b090fc09ed`.  
**Changes in Stage142:** this handoff only; production renderer, Metal backend, Vulkan/GLES, shader, RHI, geometry and test code are unchanged.

## Original Mac Studio evidence and provenance

The user uploaded `capture-20261120T133532Z-935.tar.gz` directly to this conversation. The assistant opened **that exact upload** with Python `tarfile`, rather than relying on prior Stage141 descriptions or generated CSV alone.

- **Original tar.gz SHA-256:** `159153dbb0aba51233bb708e2357d3c75d19e3a8feba75f2fe5dfc0bc6381e1e`.
- **Size / contents:** 199,198 compressed bytes; 73 tar members (72 regular files and one directory).
- **Manifest:** `stage=141`, `result=PASS`, UTC 20261120T133532Z–133602Z; 12 independent processes, 2048 authored model primitives, `mesh_subdivisions=4`, `triangle_scale=4`, 18 actual MDI submissions / 4086 real indirect commands per measured MDI frame. Flag values OFF=0 and ON=128. Parent pipeline inheritance forced OFF.
- **Native provenance:** source HEAD matches `7bfc74a0b490fc19e07b918729a113b090fc09ed`; macOS **15.6**, Mac model **Mac15,14**, Apple clang **17.0.0 (clang-1700.6.4.2)**, CMake **3.27.8**, compiled `aiverse_material_batches` SHA-256 `02abc2093840cd1d38439f7b22cf1ab46b7919406484c337f5d02af6f2b8ea7b`.

The raw archive remains the user's uploaded evidence; its bytes were not committed to GitHub. Match the checksum above before using another copy.

## Actual validation performed on archived native data

The assistant independently re-parsed **all 12 raw per-process GPU CSVs, CPU CSVs and logs**, checked timing-column validity, recalculated medians and P95, and byte-compared each archived PPM pair.

- **12/12 native processes**, with all expected OFF/ON × Direct/MDI × repeats 1/2/3 present.
- **792 valid completed Metal whole-command-buffer GPU timestamps** (66 frames, exactly 115–180, all marked valid) and **972 measured CPU render frames** (81 frames, 115–195, per process; each CPU CSV contains 106 rows, 90–195).
- All 12 logs contain `material palette CPU checks=38 result=passed`, fixed N4/triangle scale4, `pooled=1 vertex_bytes=5898240 index_bytes=393216` (6,291,456 bytes total), full shadow + opaque model passes, no test group cap, and `changed_channels=0`.
- All measured Direct frames: **0 MDI submissions / 0 indirect commands**, 2048 shadow and 2048 opaque model draws. All measured MDI frames: **18 submissions / 4086 real indirect commands**, 14 shadow and 14 opaque groups. This was checked on **every** measured frame, not inferred from the manifest.
- Six real MDI process logs each have **one** `[metal-icb-empty-tail]` marker; all six Direct controls have no marker. Each MDI submission has **227 valid draw slots, 355 reset slots**. OFF executes **227 slots**; ON executes **355 slots**, with the additional 128 reset/unencoded slots. Total for 18 submissions/frame: **4086 actual indexed draws in both modes**, and **2304 extra empty slots in the ON execution range** (6390 total executed slots vs 4086 OFF). This is a count of ICB command positions, not extra shader/draw invocations.
- All **12 primary before/after PPM pairs** and all **six extra real MDI `-mdi-after.ppm` captures** are binary-identical **within their own process**. P6 960×640, valid RGB payload. Across independent processes, there are two observed raster SHA groups (10 and 2 processes); they differ in exactly **one RGB byte** (150 vs 149), not a within-process pixel parity defect.
- Recomputed every native per-process GPU median, GPU P95/max, CPU median/P95, the three per-repeat matched-control effects, and the two aggregate summary rows, and verified numerical consistency with `stage141-runs.csv`, `stage141-paired.csv` and `stage141-summary.csv`. All consistency assertions passed.

## Measured GPU and CPU results

GPU values are medians **of three independent per-process medians**, in milliseconds. GPU is the **entire completed Metal command buffer**, not isolated ICB execution time, an individual depth/opaque pass, vertex ALU, pixel ALU, or total application FPS. A positive GPU MDI premium means MDI is slower than matched Direct.

| Executed empty ICB tail | Direct GPU median | MDI GPU median | MDI GPU premium | MDI GPU change | Direct CPU median | MDI CPU median | CPU saving |
|---|---:|---:|---:|---:|---:|---:|---:|
| OFF (`0`) | 2.007125 | 2.416935 | **+0.409810** | **+20.4178%** | 2.815040 | 1.477710 | **−47.5066%** |
| ON (`128`) | 1.994650 | 2.483750 | **+0.489100** | **+24.5206%** | 2.810000 | 1.500170 | **−46.6132%** |

Thus extending ICB execution into the 128 reset/unencoded tail positions per submission **increased the aggregate matched GPU premium by 0.079290ms** (about +19.35% versus the original 0.409810ms premium), with no reduction in valid geometry work or API draw count.

## Three process-paired controls

| Repeat | OFF MDI GPU premium | ON MDI GPU premium | OFF-minus-ON benefit (positive = ON helps) |
|---:|---:|---:|---:|
| 1 | +0.409810ms | +0.485730ms | **−0.075920ms** |
| 2 | +0.365815ms | +0.489100ms | **−0.123285ms** |
| 3 | +0.450670ms | +0.511145ms | **−0.060475ms** |

Every matched repeat shows worse whole-CB GPU time premium with the extra empty ICB execution range. This is **consistent descriptive evidence from three repetitions**, not a statistically powered per-slot GPU hardware timing or proof that empty slots cause the entire pre-existing MDI regression.

The raw GPU timelines include outliers (e.g. ON Direct repeat2 max **6.13533ms** vs median **1.99465ms**); use matched process medians and P95 and report the whole distribution if making policy decisions. The ON MDI CPU median (**1.50017ms**) is slightly above the OFF median (**1.47771ms**), while both substantially outperform their Direct CPU controls in this test; neither demonstrates a GPU win.

## Interpretation and engineering decision

**Do NOT enable `AIVERSE_METAL_CPU_MDI_EMPTY_TAIL=128` as a production path.** This flag was an opt-in *diagnostic probe*, not an optimization; extra empty ICB slots are not useful draws. The native evidence is consistent with nonzero GPU overhead for increasing the ICB executed range while keeping real work fixed. The mechanism and marginal cost per empty slot cannot be assigned from whole-CB timestamps alone.

Prior stages provide context:
- Stage122: reducing MDI submission count from 128 to 18 helped CPU, but did not resolve the GPU premium.
- Stage124: more real indirect commands at fixed submissions correlated with a growing GPU premium, though geometry/model counts also grew.
- Stage138: changing raster coverage at fixed geometry did not yield a stable differential premium across repeats.
- Stage140: skipping fragmentless depth fragment-uniform bindings was exercised, but worsened the matched GPU premium.

The shared finding across all native experiments: in this fixed synthetic 2048-primitive workload CPU MDI reduces render CPU wall time but costs more completed Metal GPU CB time than Direct. GPU counters for specific shader stages and representative game-scene FPS are still missing. **Keep CPU MDI/AUTO OFF by default**, and keep the previously rejected Stage139 skip OFF.

No AGX mitigation should be weakened: preserve `inheritBuffers=NO`, explicit per-command vertex/fragment buffers, `declareIndirectRead` resource residency, uniform snapshots, in-flight frame fences and Direct fallback. Stage141's default-unset branch preserves existing buffer capacity/reset/execute semantics, and neither Stage141 nor this documentation changes Vulkan/GLES paths.

## Changed files, tests not performed, next agent

**Stage142 changed files:** solely this `docs/agent-handoffs/batching-stage142-native-empty-tail-results-2026-11-20.md`; no C++, Objective-C++, CI or GPU code changes.

**Actually performed:** direct reading of the original uploaded 73-member native tar; SHA-256 provenance; complete 12× GPU/CPU CSV validation; independent per-frame topology counts; six actual ICB marker arithmetic checks; 12+6 binary raster comparisons; cross-process one-channel-byte difference analysis; all process-level medians/P95/max and aggregate/paired CSV recomputation. The results above are from the owner's Mac Studio archive, not synthetic test fixtures or freshly run GPU hardware by the assistant.

**Not performed:** any new assistant-run AppleClang build, new Metal benchmark run, new GPU performance counters, full CTest, Vulkan/GLES build or runtime test, representative full-game scene/FPS, additional device, statistically powered repeat sampling. GitHub Actions for the tested Stage141 HEAD were queued when initially checked; do not report a completed CI PASS without new evidence.

**Next agent:**
1. Inspect exact latest GitHub CI for Stage141 PR #93 and this results PR. Fix any genuine compiler/script errors with a new separate handoff. A docs-only PR may not rerun the GPU-focused workflow.
2. Do **not** spend further optimization cycles on empty tail slots or Stage139 fragment uniforms unless a different measured mechanism emerges. Design a controlled **real non-empty indexed command** GPU/CPU experiment at fixed geometry/material/shader, preserving Direct controls and pixel parity, potentially isolating draw distribution among ICB commands without changing the visible output. Explicitly account for instancing or scene occlusion changes; Stage124 already varied real command count with model-count confounding.
3. Use real scene asset/material workloads (opaque textured, shadow, mixed material, animated/LOD/transparent) and full-frame CPU+GPU budgets before enabling Metal AUTO or MDI globally.
4. Keep AGX buffer-binding/fencing/residency safeguards and Vulkan/GLES API parity. After every implementation stage, commit a **new handoff** listing branch, exact full SHA, code/API changes, actually completed tests/results, limitations and concrete next step.
