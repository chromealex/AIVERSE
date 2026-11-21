# AIVERSE — Stage 122: native group-size GPU scaling, 10 owner Mac Studio runs

**Date:** 2026-11-20
**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Source:** exact owner-uploaded `capture-20261120T093029Z-73785.tar.gz` (native Stage121 binary, Git HEAD `b4a5764e620d6602d89553cc4b2c095baf57ac74`).
**Stage scope:** archive analysis and this handoff only — no C++ renderer, RHI, Metal shader or AUTO policy changes.

## Exact native evidence checked

The assistant opened the full 61-member archive in Python, independently inspected `manifest.txt`, all 10 native process logs, `stage121-summary.csv`, ten per-frame GPU and CPU timelines and all before/after raster PPM files. Manifest reports `stage=121`, `result=PASS`, capture UTC `20261120T093029Z..093055Z`, 2048 authored unique primitives, order Direct1/32a/64a/128a/defaulta/defaultb/128b/64b/32b/Direct2, GPU metric *whole completed Metal command buffer timeline* (not pass-only time, GPU occupancy, FPS or GPU counters).

Each of ten runs:
- Printed `material palette CPU checks=38 result=passed`, native fixture result/final `changed_channels=0`, `GPU command-buffer timeline samples=66`; each MDI run separately `actual MDI pixel parity frame=100 changed_channels=0`.
- Has 81 measured CPU frames (115..195) and 66 valid GPU CB timestamps (115..180). All measured MDI frames use the expected real indirect path, and every test's submitted count and commands remain constant.
- Before and after PPM in **each** run are byte-identical. Across independent processes, some PPM captures differ by **one RGB channel at one pixel** from Direct1, with others identical. This is an inter-process minor raster difference, not a within-run parity failure; never claim all ten captures are byte-identical across independent process boundaries.

### Individual native timings

All units ms; summaries are per-process medians and P95:

| Trial | ICB cap | MDI submissions / frame | MDI commands / frame | GPU median | GPU P95 | CPU median |
|---|---:|---:|---:|---:|---:|---:|
| Direct1 | – | 0 | 0 | 0.573521 | 0.585708 | 2.48629 |
| Cap32a | 32 | 128 | 4096 | 0.872583 | 0.917292 | 2.62400 |
| Cap64a | 64 | 64 | 4096 | 0.8396455 | 0.862000 | 2.04329 |
| Cap128a | 128 | 32 | 4096 | 0.8152915 | 0.836542 | 1.73167 |
| Defaulta | 0 | 18 | 4086 | 0.816354 | 0.832875 | 1.47796 |
| Defaultb | 0 | 18 | 4086 | 0.8147085 | 0.838042 | 1.49642 |
| Cap128b | 128 | 32 | 4096 | 0.817083 | 0.840417 | 1.70704 |
| Cap64b | 64 | 64 | 4096 | 0.853750 | 0.894000 | 1.98475 |
| Cap32b | 32 | 128 | 4096 | 0.855021 | 0.908583 | 2.67212 |
| Direct2 | – | 0 | 0 | 0.5673125 | 0.598833 | 2.45992 |

Median **of two independent process medians** for each configuration:

| Mode/cap | GPU CB median | CPU render median | GPU vs Direct |
|---|---:|---:|---:|
| Direct | 0.57041675 | 2.473105 | baseline |
| Cap32 | 0.863802 | 2.64806 | +51.43% |
| Cap64 | 0.84669775 | 2.01402 | +48.44% |
| Cap128 | 0.81618725 | 1.719355 | +43.09% |
| Default (uncapped) | 0.81553125 | 1.48719 | +42.96% |

### Interpretation

- ICB submission count is **not irrelevant**: 128 submissions at cap32 require ~0.864ms GPU vs ~0.816ms for the default 18 submissions, ~5.6% less GPU CB time when using larger groups. CPU time falls considerably when fewer submissions are made, from ~2.648ms (cap32) to ~1.487ms (default).
- However between cap128 (32 submissions) and default (18 submissions), GPU CB median differs only ~0.000656ms (~0.08%, below what two-process samples can distinguish robustly). Even the best tested grouping has ~43% greater **whole GPU CB** time than Direct. The fixed GPU cost of each ICB dispatch cannot by itself explain the observed total GPU premium.
- Confound: cap32/cap64/cap128 emit **4096** indirect commands and no ordinary leftover Direct draws; default emits **4086** indirect commands plus 10 residual Direct draws (5 per model pass). Both render all geometry and have pixel parity. Thus the larger-group/default comparison changes slight workload composition; do not treat it as a pure isolated per-submission constant. Other possible contributors are ICB per-command GPU execution overhead, inherited/explicit resource state, Metal scheduling and shader memory access; none has been proven root cause.
- One Direct run had an isolated GPU max 5.4ms despite GPU P95 ~0.586ms, emphasizing the need to use medians and distributions rather than maxima.
- A particularly small ICB group (cap32) can be worse than Direct in **both CPU and GPU**, so don't lower the production Metal group threshold/cap or force this setting.

## Status and next step

**Actually performed:** unpacked the native owner archive with Python, read all ten logs and CSV summaries, calculated grouped medians/ratios and byte-compared each capture's PPM files, recorded this handoff on GitHub. **Not performed:** native Mac build in assistant environment, an additional experiment, per-model GPU counter timing, Xcode GPU profiler (owner's replayer crashed), representative game scene, Vulkan/GLES parity or resolution of the GPU regression.

**Next engineering hypothesis:** look for **per-command** ICB GPU overhead rather than per-submission overhead. A safe follow-up experiment should vary the *number of indirect commands* while keeping per-submission counts similar where feasible, and compare against Direct at matched authored geometry. Preserve full GPU CB timestamps, pixel parity, fixed materials, explicit Metal resource residency, pipeline state and robust fallback. Test changes only behind opt-in flags, and document every stage.

Keep CPU MDI/AUTO **OFF by default**, PR #39 **Draft** until the GPU regression is understood and representative workload GPU timing has been validated.
