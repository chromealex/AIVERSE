# AIVERSE — Stage 115: actual Direct vs MDI Xcode Metal GPU frame captures

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 (Draft)
**Stage scope:** forensic analysis of the owner-uploaded archive, documentation only; no renderer/RHI code change.

## Source / exact owner capture

User uploaded `capture-20261120T081848Z-35692.zip` containing a Stage114 archive with two real `.gputrace` packages for Direct and CPU MDI at 2048 authored unique primitives. Manifest confirms `result=PASS`, UTC 20261120T081848Z..081859Z, checkout `a324f73de954443cdcb7807008bec200a490c334`, trace Metal serial frame 131, model scoped hardware counters explicitly disabled. Both traces recorded one completed frame.

The archive was opened and inspected directly using Python's `zipfile`, `csv`, `plistlib`, byte equality and binary string searches. All following figures are sourced from the actual archive, not estimated.

### Native fixture correctness

| Check | 2048 Direct | 2048 forced CPU MDI |
|---|---|---|
| `[metal-gputrace] START/STOP frame=131` | PASS | PASS |
| `material palette CPU checks` | 38/38 PASS | 38/38 PASS |
| Baseline-vs-final raster `changed_channels` | 0 | 0 |
| Actual frame-100 MDI raster parity | Not applicable | `changed_channels=0` |
| 81 measured frames in requested mode | 81 Direct | 81 MDI |
| Real CPU MDI submissions / measured frame | 0 | 18 |
| Real CPU MDI commands / measured frame | 0 | 4086 |
| Capture native package created | yes | yes |

All four before/after PPM images for the two runs were compared within each run and are byte-identical. The Direct image and MDI image may be compared separately, but the fixture's displayed invariants are within each execution, and they pass.

### Binary GPU trace package properties

| Property | Direct | CPU MDI |
|---|---:|---:|
| Files within `.gputrace` | 38 | 135 |
| Uncompressed content bytes | 32,695,346 | 58,080,946 |
| Captured frames in Apple binary-plist `metadata` | 1 | 1 |
| `MTLIndirectCmdBuf-*` resource files | 0 | 18 |
| `MTLBuffer-*` resource files | 19 | 76 |
| Debug labels `AIVERSE Shadow Models` and `AIVERSE Opaque Models` embedded in capture streams | both | both |

This **confirms capture generation, model labeling, and Metal ICB resources present only in the MDI trace**. More resource files and a larger capture do NOT establish more GPU work or an overhead proportional to file size: the debugger trace snapshots different object and command representations.

`.gputrace` is a proprietary Apple/Xcode format. Binary inspection of its `capture`/`unsorted-capture` files and plist metadata does **not** provide a trustworthy GPU model-pass execution duration. The exact shadow/opaque GPU times must be inspected using Xcode's GPU frame debugger or Instruments on a supported Mac.

### Capture-heavy CPU data are not performance benchmarks

This native run displays unusual capture-induced CPU wall medians:
- Direct `renderer.render()` median: **3.23008ms**, P95 **3.45758ms**.
- Forced CPU MDI median: **6.23692ms**, P95 **6.49317ms**, maximum including a frame at **252.046ms**.
- The captured MDI run's accumulated CPU backend submission cost is substantially higher than uncaptured runs; `AIVERSE` explicitly instruments capture recording.
- Therefore **do not** interpret the capture run's MDI CPU slowdown as a normal game performance regression; compare the earlier matched **uncaptured** Stage109 measurement (CPU MDI lower CPU wall ~35.6% at 2K while GPU whole-CB timeline +37.3%).
- The trace's number of `MTLIndirectCmdBuf` resources (18) matches the 18 CPU MDI submissions in a measured frame, but trace-resource snapshots can include retained command objects and should not be used as performance duration.

## Verified code, tests, limitations

**Performed in assistant environment:** exact ZIP listing and extraction/stream reads, log inspection for both modes, CSV timing calculations, independent byte comparison of within-run PPM, Apple `metadata` parsing, ICB resource inventory and model debug label identification; GitHub handoff creation. **Owner previously performed:** full native AppleClang compilation of Stage114 and both captured runs with PASS. **Not performed:** actual `.gputrace` replay in Xcode GPU frame debugger; hardware-specific pipeline counters or per-encoder GPU duration extraction; GPU fixes or source changes; 4K frame captures; Vulkan/GLES tests; production scenes.

## Next concrete action for owner / next agent

Open **both** existing traces in Xcode on Mac Studio:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage114
DIR=build/batching-stage114/traces/capture-20261120T081848Z-35692
open -a Xcode "$DIR/2048-direct.gputrace"
open -a Xcode "$DIR/2048-mdi.gputrace"
```

In Xcode, compare the capture's render encoders/groups `AIVERSE Shadow Models` and `AIVERSE Opaque Models`. Record GPU debugger's available duration/occupancy/cost metrics per encoder or expensive draw, and identify whether MDI `executeCommandsInBuffer` is especially costly in shadow, opaque, or both. If the debugger only provides a command stream without per-draw timings, say so and share available screenshots/Performance HUD or GPU counters; do not invent times. Inspect shader/pipeline/resource state and residency too. Screenshots from Xcode GPU profiler or its exported summary can be shared to drive a narrow optimization.

**Engineering disposition:** CPU MDI remains opt-in and PR #39 Draft because Stage109 reproducibly shows GPU whole-command-buffer regression (+37.29% on 2K, +54.28% on 4K) even with CPU savings; no reduction of Metal ICB safety bindings or arbitrary AUTO threshold changes without evidence. Preserve default-off flags and produce a new per-stage handoff after every subsequent development stage.
