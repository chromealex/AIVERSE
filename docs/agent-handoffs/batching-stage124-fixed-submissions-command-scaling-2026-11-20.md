# AIVERSE — Stage 124: fixed-submission GPU MDI command-scaling evidence

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Stage:** independent owner-archive analysis; handoff-only change, **no renderer/Metal source changes**.

## Exact source and native provenance

The user uploaded `capture-20261120T093909Z-77250.tar.gz`, the output of Stage123's existing compiled Stage121 Mac Studio fixture. The assistant opened this precise archive locally, independently read `manifest.txt`, `stage123-runs.csv`, `stage123-comparison.csv`, all **12 native process logs**, all 12 CPU timeline CSVs and all 12 GPU timeline CSVs, and compared the 12 before/after PPM pairs byte-for-byte. The archive has 70 entries. Manifest records `stage=123`, `result=PASS`, UTC start `20261120T093909Z`, end `20261120T093945Z`, Git detached source HEAD `b4a5764e620d6602d89553cc4b2c095baf57ac74`, executable `build/batching-stage121/aiverse_material_batches`. Three authored model primitive counts and diagnostic CPU MDI group caps: 1024/32, 2048/64, 4096/128; for each, process order Direct1/MDI1/MDI2/Direct2.

Every native log includes `material palette CPU checks=38 result=passed`, `changed_channels=0`, successful 66-sample completed Metal GPU CB timeline; every forced-MDI process separately reports `actual MDI pixel parity frame=100 changed_channels=0`. Within-run before/after PPM bytes are identical for **all 12**. Direct runs use 81 measured frames with 0 MDI submissions and 0 commands; MDI runs use all 81 measured frames with exactly **64** actual MDI submissions, **2×items** indirect commands and **32 shadow + 32 opaque** API model draw groups per frame. No changing frame workload detected. Total independently validated observations: 12×66 = **792 valid completed whole-GPU-command-buffer timestamps** and 12×81 = **972 CPU measured frames**. No GPU execution fault was observed in the submitted native logs.

## Individual run values

All timing values in milliseconds. GPU is **entire completed Metal command-buffer interval**, not isolated GPU model-pass duration or FPS.

| Items | Trial | GPU median | GPU P95 | CPU render median | MDI submissions | MDI commands |
|---:|---|---:|---:|---:|---:|---:|
| 1024 | Direct1 | 0.357833 | 0.370541 | 1.26971 | 0 | 0 |
| 1024 | MDI1 cap32 | 0.516500 | 0.524250 | 1.56304 | 64 | 2048 |
| 1024 | MDI2 cap32 | 0.556375 | 0.582292 | 1.37825 | 64 | 2048 |
| 1024 | Direct2 | 0.355438 | 0.558000 | 1.22871 | 0 | 0 |
| 2048 | Direct1 | 0.577271 | 0.603834 | 2.40229 | 0 | 0 |
| 2048 | MDI1 cap64 | 0.870750 | 0.913875 | 2.06296 | 64 | 4096 |
| 2048 | MDI2 cap64 | 0.837667 | 0.876709 | 2.00283 | 64 | 4096 |
| 2048 | Direct2 | 0.575833 | 0.596166 | 2.50296 | 0 | 0 |
| 4096 | Direct1 | 0.9474995 | 0.969625 | 4.93800 | 0 | 0 |
| 4096 | MDI1 cap128 | 1.480790 | 1.504960 | 3.55692 | 64 | 8192 |
| 4096 | MDI2 cap128 | 1.376000 | 1.392330 | 3.31988 | 64 | 8192 |
| 4096 | Direct2 | 0.9466875 | 1.008830 | 4.83483 | 0 | 0 |

## Matched summary: median of two process medians each mode

| Items | Indirect commands per MDI frame | Fixed MDI submissions | GPU Direct median | GPU MDI median | GPU MDI premium | GPU MDI change | CPU Direct median | CPU MDI median | CPU MDI change |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 2048 | 64 | 0.3566355 | 0.5364375 | 0.1798020 | +50.416% | 1.249210 | 1.470645 | **+17.726%** |
| 2048 | 4096 | 64 | 0.5765520 | 0.8542085 | 0.2776565 | +48.158% | 2.452625 | 2.032895 | **−17.114%** |
| 4096 | 8192 | 64 | 0.9470935 | 1.4283950 | 0.4813015 | +50.819% | 4.886415 | 3.438400 | **−29.633%** |

GPU premium (whole CB MDI minus Direct) climbs ~0.180 → 0.278 → 0.481ms as command count climbs 2048 → 4096 → 8192 while ICB submission count remains exactly 64. Relative GPU difference is remarkably consistent ~48–51%. This is evidence *against* an explanation dominated solely by a fixed per-submission GPU cost. Extra per-command ICB execution / resource state or workload-dependent GPU cost are candidates, not established causes. The workloads also differ in primitive count, geometry, shader execution and memory/buffer footprint; no exact marginal GPU cost per command is identified. The analyzer's `premium_per_command_ms_descriptive_only` field is an arithmetic normalization, **not** a measured per-command hardware penalty.

At 1024 items and 32-command groups, CPU MDI **loses** to Direct by 17.7% CPU wall (and by ~50.4% GPU whole CB); small groups should not be forced. In uncapped Stage121 baseline CPU MDI won on CPU but still lost on GPU, supporting preservation of the established default group minimum and original grouping until specific GPU optimization evidence exists.

## Status and next technical experiment

**Actually performed:** direct local Python/tarfile/CSV parsing of exact uploaded archive; independent verification of 12 logs, 12 GPU/CPU frame sequences, per-frame MDI workload invariants, native 38 checks and pixel parity using byte comparison; arithmetic checks against archived stage123 comparison; wrote this GitHub handoff. **Not performed:** assistant-side Mac native compile or GPU execution, new Metal optimization, per-model GPU counter timings (unsupported on owner's device), Xcode Performance replay (previously segfaulted), Vulkan/GLES verification, representative real-scene GPU performance or FPS.

**Proposed next stage:** identify whether excess GPU CB cost tracks **shader/vertex workload complexity** versus ICB per-command binding/execution overhead with a tightly controlled shader/geometry comparison at a *fixed primitive count and identical model/pass submissions*. For example, use a test-only material-quality/shader permutation or vertex workload level that preserves API draw and indirect counts, run matched Direct and MDI controls, verify pixel parity within each test configuration, and observe whether MDI GPU premium changes with shader workload. These comparisons are diagnostic, not direct GPU subpass timings. Do not alter default production shaders or safety-critical Metal ICB parent/child explicit buffer bindings, `declareIndirectRead` residency, draw command lifetime or frame fences. Keep CPU MDI/AUTO default OFF and PR #39 Draft.

**Continuation requirement:** After next code stage, update its dedicated handoff with changed paths/API, tests actually executed, limitations, Git commit/branch and concrete next Mac Studio command.
