# AIVERSE Stage 110 — repeated Metal GPU Direct vs CPU MDI regression validated

**Date:** 2026-11-20
**Repo:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119` (PR #39, Draft)
**Type:** independent owner-archive analysis and handoff only. No production renderer, Metal RHI, CPU AUTO policy, test fixture or runner changes.

## Owner's actual Stage109 native evidence

The user uploaded **`capture-20261120T074638Z-25565.tar.gz`**. The assistant extracted the exact archive in the container and inspected manifest, stage109 summary, all eight process logs, eight CPU frame CSVs (106 rows, measured frames 115..195) and eight completed GPU command-buffer CSVs (66 tags 115..180). Manifest says:
- `stage=109`, source worktree HEAD `f0ba60f3c3caf867156d762936127087e945e25c`, UTC `20261120T074638Z`–`20261120T074704Z`, `result=PASS`.
- Four process runs each at 2048 and 4096 unique authored primitives; order Direct1/MDI1/MDI2/Direct2.
- Existing Stage108 binary `build/batching-stage108/aiverse_material_batches`; all GPU figures are **whole Metal command-buffer timeline durations** (completed `GPUStartTime/GPUEndTime`), *not isolated model-pass GPU time* or FPS.

### Individual native process measurements

All durations in milliseconds, GPU and CPU measured in their respective samples:

| Items | Trial | GPU CB median | GPU P95 | GPU max | CPU render median | CPU P95 |
|---:|---|---:|---:|---:|---:|---:|
| 2048 | Direct1 | 0.6230625 | 0.634833 | 0.645958 | 2.29717 | 2.39417 |
| 2048 | MDI1 | 0.8226455 | 0.900959 | 1.34946 | 1.49779 | 1.62450 |
| 2048 | MDI2 | 0.8097080 | 0.841500 | 0.891875 | 1.46483 | 1.51729 |
| 2048 | Direct2 | 0.5659165 | 0.633333 | 1.671000 | 2.30208 | 2.40554 |
| 4096 | Direct1 | 0.9442915 | 0.994500 | 2.384290 | 4.63296 | 4.77042 |
| 4096 | MDI1 | 1.4503550 | 1.464580 | 1.477960 | 3.04217 | 3.24067 |
| 4096 | MDI2 | 1.4370200 | 1.510080 | 3.087920 | 3.12462 | 3.36375 |
| 4096 | Direct2 | 0.9271670 | 0.935083 | 0.946250 | 4.51792 | 4.67546 |

Medians of the **two independent process medians per mode**:
- 2048 GPU Direct **0.5944895ms**, GPU CPU-MDI **0.81617675ms**, **+37.29% GPU CB duration** for MDI. CPU Direct **2.299625ms**, CPU MDI **1.48131ms**, **−35.58% CPU wall**.
- 4096 GPU Direct **0.93572925ms**, GPU CPU-MDI **1.4436875ms**, **+54.28% GPU CB duration** for MDI. CPU Direct **4.57544ms**, CPU MDI **3.083395ms**, **−32.61% CPU wall**.

These repeat Stage108's direction of GPU regression (earlier single-process +47.3%/+40.5%) despite counterbalanced D/M/M/D process order. Order can still affect results; do not conflate these percentages with a GPU occupancy metric, model-pass-only GPU duration, whole-app frame time or FPS.

### Correctness/per-frame accounting validated from archive

- All eight logs print `material palette CPU checks=38 result=passed`, end-image parity `changed_channels=0`, `GPU command-buffer timeline samples=66`. All eight pairs of `before.ppm` and `after.ppm` verified identical by byte comparison. All four MDI logs also print `actual MDI pixel parity frame=100 changed_channels=0`.
- All measured Direct frames have `mdi_submissions=0`, `mdi_commands=0`. All measured 2048 MDI frames have **18** real MDI submissions and **4086** commands. All measured 4096 MDI frames have **36** real submissions and **8172** commands. MDI per-run measured submissions: **1458** at 2K and **2916** at 4K.
- All eight GPU timestamp CSVs contain 66 unique ordered tags 115..180 with valid completion; total 528 completed GPU observations. GPU max spikes are not sufficient to explain the direction of median regression.
- At 2K/4K the short unbatched remainders are already accounted for by ordinary Direct draws and do not indicate missing image geometry (Stage107).

## Existing RHI support and missing instrumentation

Read `engine/gfx/metal/GfxMetal.mm` and `engine/gfx/Gfx.h` via GitHub. `timeGpuFrame(tag)` and `takeGpuTimings()` already read **whole command-buffer** completed timestamps. `GfxMetal.mm` currently has no `MTLCounterSampleBuffer` / render-stage counter sampling mechanism, and the RHI does not expose model-phase-specific GPU timestamps. Do **not** claim that the existing API already measures GPU model rendering alone.

The confirmed whole-GPU-CB cost makes CPU MDI default enablement unjustified despite CPU savings. Next engineering work should introduce an **opt-in, capability-gated Metal GPU counter-sample / scoped timing experiment** for the shadow and opaque model passes, with a robust unsupported path and no broken Metal command buffer synchronization. Alternatively capture the existing fixture via Xcode Instruments Metal GPU Frame Capture to inspect ICB GPU commands, pipeline state/resource binding and GPU counters. Avoid disabling explicit buffer binds, residency declarations, frame fences or driver-crash mitigations based on incomplete profiling.

## Validation truth / continuation

**Actually performed:** parsed uploaded archive and all native process logs/CSV, recomputed all medians/P95/max and raster parity, inspected existing Metal timing paths through GitHub, created this handoff on feature branch. **Not done:** assistant-side native AppleClang compile, GPU counter-sample implementation, isolated shadow/opaque GPU costs, 10K representative scene, Vulkan/GLES parity, CI, independent FPS. Keep PR #39 **Draft** and CPU MDI/AUTO opt-in (off by default).

**Next:** add a dedicated, safely optional pass-specific GPU sampling contract to `gfx::Device` and implement Metal counter sample capability detection. Ensure all unimplemented backend paths degrade explicitly (no fake valid zero times); then run 2K/4K Direct/MDI with exact pixel parity and performance comparisons, preserving the previous full-command-buffer timestamps as control. A separate handoff is required after each implementation stage.
