# AIVERSE — Stage 126: Low / High material-quality CPU MDI GPU trial results

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Stage type:** native owner archive validation + agent handoff only. **No renderer, shader, Metal, RHI, Vulkan, GLES or AUTO code changed.**

## Source and native provenance

The owner submitted exactly `capture-20261120T094744Z-79217.tar.gz`, produced by Stage125's `tools/batching-stage125-trials.sh` using already built Stage121 Mac Studio executable. The archive was read directly with Python `tarfile`; manifest says `stage=125`, `result=PASS`, UTC 20261120T094744Z–094804Z, `git_head=b4a5764e620d6602d89553cc4b2c095baf57ac74`, 2048 authored model primitives, Quality Low=0 and High=2, test order `low-direct1,low-mdi1,high-direct1,high-mdi1,high-mdi2,high-direct2,low-mdi2,low-direct2`. Archive has 48 files, including logs, per-frame GPU/CPU CSVs, raster PPM before/after pairs, and generated summary/comparison CSVs.

Verified independently against the actual archive:
- **8/8 processes PASS**, eight logs include 38 CPU invariant checks passed, 66 valid completed Metal GPU CB timestamps per run = **528 GPU observations**.
- Eight per-frame CPU timelines have 106 frames each, with 81 measured CPU frames each = **648 measured CPU observations**.
- Every one of the 8 before/after PPM pairs matches **byte-for-byte within the same run**. Forced-MDI runs also record `actual MDI pixel parity frame=100 changed_channels=0`.
- All Direct processes report 0 MDI submissions and 0 indirect commands per measured frame, shadow_draws=2048 and opaque_draws=2048. All MDI processes report exactly **18 ICB submissions, 4086 indirect commands, 14 model draw groups per shadow/opaque pass** and 81 forced MDI frames (1458 summed MDI submissions) per process.
- Low vs High rendered images are **not identical across quality settings** (1296 image data/header bytes differ between the first two Direct captures), so the toggle demonstrably changes raster output; it does not prove how many shader instructions or samples were executed or how expensive each shading variant is.

## Individual run results

Milliseconds. GPU is **whole completed Metal command-buffer duration**, NOT model-pass-only GPU time, shader instruction cost, GPU FPS or visual quality.

| Quality | Mode / repeat | GPU median | GPU P95 | CPU render median | MDI submissions | MDI commands |
|---|---|---:|---:|---:|---:|---:|
| Low | Direct1 | 0.5928545 | 0.603583 | 2.45850 | 0 | 0 |
| Low | MDI1 | 0.8498750 | 0.874542 | 1.49588 | 18 | 4086 |
| High | Direct1 | 0.5936250 | 0.612625 | 2.60075 | 0 | 0 |
| High | MDI1 | 0.8626670 | 0.884041 | 1.51737 | 18 | 4086 |
| High | MDI2 | 0.8124165 | 0.832042 | 1.56108 | 18 | 4086 |
| High | Direct2 | 0.5656670 | 0.587417 | 2.55629 | 0 | 0 |
| Low | MDI2 | 0.8415000 | 0.855291 | 1.43263 | 18 | 4086 |
| Low | Direct2 | 0.5699375 | 0.593750 | 2.59704 | 0 | 0 |

## Paired aggregate: median of two process medians per mode

| Quality | Direct GPU CB median | MDI GPU CB median | MDI GPU premium | MDI GPU change | Direct CPU render median | MDI CPU render median | MDI CPU change |
|---|---:|---:|---:|---:|---:|---:|---:|
| Low | 0.581396 ms | 0.8456875 ms | 0.2642915 ms | +45.4581% | 2.527770 ms | 1.464255 ms | -42.0733% |
| High | 0.579646 ms | 0.83754175 ms | 0.25789575 ms | +44.4919% | 2.578520 ms | 1.539225 ms | -40.3059% |

**Interpretation:** On this specific 2048-primitive test, Low vs High material quality has little effect on the measured GPU premium of CPU MDI compared with Direct (~0.264ms vs ~0.258ms). Differences are ~0.0064ms and there are only two independent process medians per mode/quality: do not claim this is a statistically significant shader-quality effect, and do not claim shader workload is identical. The fixed model topology verifies that an admission/grouping difference did not confound the comparison. The baseline +44–45% whole-CB GPU regression remains, with ~40–42% CPU wall benefit. Cross-quality raster difference confirms this is not precisely the same image, but validates normal quality variation. Intra-mode and inter-run timing drift is visible (e.g. High MDI1 GPU 0.862667ms vs High MDI2 0.8124165ms), so stronger repetition would be required to claim a quality-dependent effect.

## Technical next step

The evidence from Stages119–125 is that the GPU premium persists across shadow-only and opaque-only passes, submission group sizes, scene size at fixed 64 submissions, and Low/High material-quality variants. The mechanism remains **unproven**. Prioritize reviewing how the CPU Metal ICB encoder configures inherited pipeline, per-command vertex/fragment argument binding and the actual GPU cost of its per-command dispatch — **without removing** the AGX crash mitigations, explicit safe pipeline/buffer state, `useResource` residency declarations, fences or command lifetime restrictions.

A follow-up experiment with a **measurably different vertex/shader workload while holding draw count and topology fixed**, or a supported alternative hardware/driver trace, would be more diagnostic than another few repetitions of this weak Low/High difference. An opt-in synthetic shader ALU variant is permissible only if both Direct and MDI use identical shader pipeline semantics, pixel parity is checked within each variant, and production shaders remain unchanged. It must not be confused with real-scene performance evidence. Avoid turning on default MDI until its GPU performance is acceptable for representative content.

## Files / tests / state

- **Changed in Stage126:** only `docs/agent-handoffs/batching-stage126-material-quality-native-results-2026-11-20.md`.
- **Actually performed:** Python-based archive examination, 8 log checks, 528 GPU entries / 648 measured CPU frames, exact per-frame topology invariant checks and byte comparisons of 8 PPM pairs, reconciled all summary medians with native CSV.
- **Not performed:** new Mac native compile, actual new GPU execution, GPU model-pass counter sampling (hardware unsupported), Xcode GPU replay profiling (previously replayer segfault), Vulkan/GLES, full CTest, shader complexity instrumentation, real game FPS or root cause resolution.
- **PR #39 stays Draft**. CPU MDI and AUTO remain OFF by default. Next agent should add separate handoff for any subsequent code changes, actual tests and exact branch/commit.
