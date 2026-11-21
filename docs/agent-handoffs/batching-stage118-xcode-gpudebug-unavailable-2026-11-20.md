# AIVERSE — Stage 118: Xcode 26.3 GPU debugger CLI unavailable

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Type:** owner log review and handoff only; no engine code changes.

## Owner native result

The owner executed:

```text
xcodebuild -version
xcrun --find gpudebug
xcrun gpudebug --help
```

Exact output:

```text
Xcode 26.3
Build version 17C529
xcrun: error: unable to find utility "gpudebug", not a developer tool or in PATH
```

Both attempts to discover and execute the `gpudebug` utility failed. No CLI GPU debugger is therefore available through `xcrun` in this current developer installation. This is **not** a compilation error or proof of a missing project dependency; do not instruct the user to repeat `xcrun gpudebug`, assume it exists at an undocumented filesystem path or suggest reinstalling Xcode without further evidence.

## Context from actually verified earlier stages

- Stage 114–115 native tests produced and Xcode successfully opened real Direct/CPU MDI `.gputrace` captures at 2048 unique primitives, with pixel parity and 38 CPU checks PASS in both modes.
- Stage 116 Xcode Summary screenshot displayed 4098 Direct API draw calls vs 30 CPU MDI API draw calls, two render encoders and one command buffer each, but **Performance data not available** on both.
- Stage 117 Xcode GPU Replayer crashed when attempting profiling: `Unexpected Replayer Termination — Segmentation fault: 11`.
- Separate *uncaptured* Stage109 completed Metal GPU command-buffer timestamps: CPU MDI whole-GPU-CB median +37.3% at 2K and +54.3% at 4K vs Direct; CPU `renderer.render()` wall medians were lower. These figures should not be equated with model-pass-only GPU timings or FPS.
- Stage111 hardware draw-boundary GPU counter sampling was unsupported on the owner's Mac.

## Next workflow without new compile or assumed CLI

1. Reopen `2048-direct.gputrace` **without pressing Profile**, expand `Command Buffer 0` and both render encoders and the `AIVERSE Shadow Models` and `AIVERSE Opaque Models` debug labels if Xcode exposes them. Capture screenshots of the command tree and any per-encoder GPU metrics actually displayed.
2. Repeat for `2048-mdi.gputrace`, looking for the `executeCommandsInBuffer` entries and ICB command/resources. Avoid inferring GPU runtime from API draw call count or capture archive size.
3. If opening command trees also causes a GPU Replayer crash, examine `~/Library/Logs/DiagnosticReports/` for replayer crash reports and provide a stack trace; optionally use the installed Instruments.app GPU tooling if the selected device supports it. Do not assume that Instruments always offers per-draw time.
4. If no supported per-pass profiling is available, proceed with a *controlled, opt-in GPU whole-CB A/B experiment* that disables one model pass at a time **only in the test fixture**, retaining pixel-parity checks for full runs and never changing production renderer output, or use Metal GPU capture on other supported hardware. A one-pass experiment changes the workload and cannot be interpreted as independent per-pass GPU time without a well-designed baseline.

## Status / tests

**Actually performed:** inspected owner-supplied Xcode command log and prior capture/replayer screenshots; wrote handoff. **Not performed:** own Xcode CLI invocation, native compile, Instruments capture, model-pass GPU timings or Metal ICB optimization. PR #39 stays Draft, CPU MDI/AUTO defaults unchanged (OFF), explicit buffer binding and residency protections preserved.

**Next agent:** prioritize interpreting real Xcode command tree screenshots if they are available, rather than spending more cycles on `gpudebug` discovery. Record every actual code change and test evidence in a separate stage handoff.
