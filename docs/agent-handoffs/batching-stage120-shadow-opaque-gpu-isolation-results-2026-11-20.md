# AIVERSE — Stage 120: GPU model-workload isolation results

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Type:** owner archive analysis and handoff only; no production rendering code changed.

## Source and validation

Owner uploaded `capture-20261120T091210Z-64556.tar.gz`, the output of Stage119's six native test processes on Mac Studio. This assistant extracted the exact archive using Python's `tarfile` and parsed `manifest.txt`, `stage119-summary.csv`, all six native logs, six 106-row CPU per-frame timelines, six 66-row GPU command-buffer timestamp timelines, and the before/after PPM raster captures.

Manifest confirms `stage=119`, UTC 20261120T091210Z–091227Z, source checkout `a28634a42bd74b31e64bab22f03b337ad8c78535`, items=2048, phases=full,shadow,opaque, modes=direct,mdi, `result=PASS`. GPU draw-boundary hardware counters were disabled; the valid GPU metric is entire Metal command buffer duration, not individual model-pass time.

All six logs record 38/38 CPU invariants PASS, valid 66 whole-CB GPU timestamps, and `changed_channels=0`. All six `before.ppm` and `after.ppm` image byte pairs were **independently verified identical**. Three MDI modes additionally show frame-100 real MDI capture parity `changed_channels=0`. Every measured Direct frame has zero MDI; all MDI frames have 81/81 measured frames with actual nonzero submissions and stable command counts.

### Measured results (all ms, medians)

| Workload | Mode | GPU CB median | GPU CB P95 | CPU render() median | CPU P95 |
|---|---|---:|---:|---:|---:|
| full | Direct | 0.574354 | 0.596125 | 2.43896 | 2.51688 |
| full | MDI | 0.8089165 | 0.879792 | 1.56283 | 1.68550 |
| shadow-only | Direct | 0.1767705 | 0.209334 | 1.34821 | 1.40892 |
| shadow-only | MDI | 0.336875 | 0.354041 | 0.846625 | 0.94425 |
| opaque-only | Direct | 0.5855625 | 0.608250 | 1.29579 | 1.37363 |
| opaque-only | MDI | 0.7366455 | 0.776500 | 0.826292 | 0.875291 |

Relative GPU CB MDI regression when comparing **the same modified workload**: full +40.84%, shadow-only +90.57%, opaque-only +25.80%. Relative CPU render() savings for MDI in the same paired comparison: full −35.92%, shadow-only −37.20%, opaque-only −36.23%.

Workload accounting: full MDI 18 submissions / 4086 indirect commands per measured frame; shadow-only MDI 9 / 2043; opaque-only MDI 9 / 2043. Shadow-only Direct measured `shadow_draws=2048,opaque_draws=0`; opaque-only Direct `shadow_draws=0,opaque_draws=2048`. MDI single-pass `14` API model draws in the respective enabled phase; full MDI `14+14`. Both render encoders persist, and the floor/non-model draws remain.

## Interpretation / important limitations

The **GPU CB regression occurs in both shadow-only and opaque-only experiments**, pointing away from a bug solely confined to one model pass. A common ICB execution/vertex binding/driver path is a plausible next target, not an established cause. Both shadow and opaque may have different shader and bandwidth paths; model-pass-exclusive GPU time is **not measured** by this experiment.

Notable non-additivity: opaque-only Direct GPU CB median (0.58556ms) exceeds the *full* Direct GPU CB median (0.57435ms). One-pass suppression changes shadow-texture content, render dependencies and GPU scheduling, so **do not** subtract full-minus-one-pass CB medians to claim numerical shadow/opaque GPU costs. Relative changes within a fixed workload are useful only as diagnostic evidence.

The capture/Replayer problems remain: Stage111 Metal draw-boundary counters unsupported, Stage117 Xcode profiling replayer segfault 11, no gpudebug CLI in owner's Xcode 26.3. Native GPU completed whole-CB timestamps remain valid. Stage119 native fixture and branch code compiled and ran successfully as confirmed by archived logs; assistant did not run Mac GPU tests locally.

## Source review / next engineering experiment

Reviewed `engine/gfx/metal/GfxMetal.mm` through GitHub: CPU MDI path has explicit per-ICB pipeline/buffer binds, `declareIndirectRead` residency and `executeCommandsInBuffer`, plus an AGX G15 crash mitigation against inherited zero-count vertex buffers. **Do not disable these bindings/residency protections to chase performance.**

Recommended next small controlled experiment, **only in an opt-in test**: keep the exact same Direct/MDI workload and shader/material state, compare ICB submission group size/size threshold and GPU CB median/P95 (e.g. actual MDI count and command count per frame, with 16-command default threshold and safe group caps), while preserving pixel parity. This may reveal per-ICB fixed costs without changing driver-sensitive parent/child bindings. Use D/M/M/D counterbalanced processes and retain GPU CB timestamps, not Xcode captures, to quantify outcomes. If result shows per-submission fixed overhead, optimize grouping limits within existing safe ICB resource lifetime strategy; if not, investigate per-command ICB GPU scheduling and state binds with Xcode command list rather than relying on missing hardware counters. More representative real-scene workload and Vulkan/GLES parity remain pending.

## Changed files, tests and handoff

**Changed in Stage120:** only this separate handoff file. **Actually tested:** exact owner archive parsing, verified six logs / twelve images / GPU+CPU CSV contracts. **Not tested:** independent AppleClang build in assistant, GPU pass-specific counter data, Vulkan/GLES, full CTest or performance of any new ICB optimizer. **No claim of resolved GPU regression.**

Keep CPU MDI and AUTO opt-in/OFF by default, PR #39 Draft, and create a separate agent handoff after the next code stage.
