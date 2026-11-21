# AIVERSE batching — Stage 107: verified 2K/4K MDI residual Direct draws

**Date:** 2026-11-20
**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39)
**Scope:** forensic review of the *actual* uploaded `capture-20261120T071923Z-97395.zip`; documentation only. No engine/renderer source modifications.

## Owner-provided evidence and validation

The archive contains eight native Mac Studio execution logs, eight 106-row timeline CSVs, `stage104-summary.csv`, original `before.ppm` and final `after.ppm` images for every trial, and an additional `mdi-after.ppm` for each MDI trial. All logs show CPU unit checks PASS (38), `changed_channels=0` for before/after in each run, and real MDI frame 100 parity `changed_channels=0` where applicable; every MDI trial had 81 measured frames and no Direct-only measured frames, and every Direct trial had 81 Direct measured frames, zero indirect submissions. No Metal renderer error reported.

| Items | Direct model draws / pass | MDI submissions / pass | CPU MDI commands / two passes | Remaining plain draws / pass | Ratio of authored pass primitives encoded in MDI |
|---:|---:|---:|---:|---:|---:|
| 2048 | 2048 | 9 | 4086 | 5 | 99.75586% |
| 4096 | 4096 | 18 | 8172 | 10 | 99.75586% |

The rendered item counts reconcile exactly: `2048*2 - 4086 = 10 = 5*2`; `4096*2 - 8172 = 20 = 10*2`. The log's `batch=1 shadow=14 opaque=14` at 2K, and `shadow=28 opaque=28` at 4K, reconcile with `9 MDI + 5 Direct` and `18 MDI + 10 Direct` respectively. (Those profile draw counts encompass both paths and should not be treated as indirect commands.) Therefore **the missing MDI commands are accounted for by Direct fallback, not by missing rasterized geometry**.

The code path in `engine/render/Renderer3DModels.cpp` uses a Metal MDI minimum group size of **16 commands by default** (`indirectCommandThreshold()`, override `AIVERSE_METAL_MDI_MIN_COMMANDS=2..32`). It creates MDI only for compatible runs meeting that threshold; smaller leftover groups fall through to `drawGeometryBatch(...)`, which retains the ordinary RHI draw route. The observed groups are consistent with 227-command MDI group limits and short per-pass Direct tails (5 or 10 items). The specific reason for the 227-item group boundary remains to be established; it should not be called a proven fixed API limit.

Per-run captured PPM bytes verified directly in Python: `before.ppm` and `after.ppm` are **identical for each of the eight runs**. For 2K all four `after.ppm` are identical across separate processes. For 4K the first Direct and both MDI `after.ppm` images are identical; second Direct run is different in **exactly one RGB channel by one intensity unit** at one pixel. This inter-process difference is outside the fixture's within-process capture check and does **not** suggest missing meshes or an MDI-vs-Direct regression; nevertheless cross-run byte identity must not be claimed for all eight images.

### Actual CPU results from complete archive

| Items | Direct pair median of per-process medians | MDI pair median of per-process medians | MDI CPU wall reduction |
|---:|---:|---:|---:|
| 2048 | 2.3531ms | 1.5972ms | 32.12% |
| 4096 | 4.6161ms | 2.9476ms | 36.14% |

These are synchronous CPU `renderer.render()` stopwatch values, **not** GPU time or FPS. All 2K and 4K run logs report native PASS and byte-identical MDI capture relative to their own before image. Small inter-process image difference should be inspected only if a broader determinism guarantee is required.

## Files, actual tests, limitations

**Files changed in this stage:** only this handoff document. **Tests performed:** independently inspected the eight logs and screenshots and analyzed the CSV-containing ZIP in the assistant's Python runtime; examined `Renderer3DModels.cpp` code through GitHub connector. **Not done:** native Mac build/test in assistant environment, GPU timestamps, Vulkan/GLES parity, real-game content or 10K stress. Existing runnable tests were executed by the owner on Mac Studio.

**Next:** preserve default Metal MDI minimum 16; do not force tiny residual groups into ICB to chase 100% MDI command coverage. Add focused per-group accounting diagnostic only if needed for exact 227 boundary explanation; GPU execution profiling and more representative real-scene loads matter more to the decision to enable MDI by default. Keep PR #39 Draft until multi-platform/representative workload parity is complete.
