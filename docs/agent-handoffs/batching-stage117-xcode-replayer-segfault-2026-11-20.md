# AIVERSE — Stage 117: Xcode GPU Replayer segmentation fault

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Branch:** `perf/auto-batch-cost-metrics-20261119`
**PR:** #39 Draft
**Scope:** evidence review and fallback procedure only; no code changes to Metal/CPU-MDI or shaders.

## Actual evidence

Owner supplied a screenshot showing an Xcode GPU Debugger error dialog:

```text
Unexpected Replayer Termination
Segmentation fault: 11
```

This follows Stage116 screenshots successfully opening the 2K Direct and CPU MDI `.gputrace` files with Xcode Summary and showing one command buffer, two render encoders, 4098 Direct API draw calls vs 30 MDI API draw calls, and no GPU Performance data. The immediate user action preceding the crash was attempting to obtain Performance profiling. We **cannot establish from the single screenshot which trace caused the crash**, or the exact failing thread/API/driver.

**Interpretation:** the crashed process is the Xcode GPU trace replayer. It is **not** evidence that the original AIVERSE native fixture crashed or that CPU MDI rendered incorrectly. Stage114 native capture had completed with 38 CPU checks PASS and byte-for-byte pixel parity for both modes. This error also does **not** invalidate Stage108/109 independent whole-command-buffer GPU timestamps, which established a repeatable GPU CB regression for CPU MDI on synthetic 2K/4K workloads.

Apple documentation distinguishes GPU trace replay with optional profiling (replay can display the command list without profiling). Apple also documents the interactive `gpudebug` CLI for GPU trace inspection, availability depending on installed Xcode version and selected developer directory. Apple Developer Forums have acknowledged replayer segfaults and suggested filing Feedback with the GPU trace and crash report; this is a possible debugger/tool defect, **not** a proven AIVERSE/Metal driver defect.

Official docs:
- https://developer.apple.com/documentation/xcode/replaying-a-gpu-trace-file
- https://developer.apple.com/documentation/xcode/metal-debugger
- https://developer.apple.com/forums/thread/826755

## Recommended next Mac action (no engine rebuild)

1. Reopen `2048-direct.gputrace` without selecting Profile GPU Trace / Profile After Replay. Confirm whether Summary and the Command Buffer 0 → Render Encoders tree remain navigable.
2. Repeat with `2048-mdi.gputrace` **without profiling**. Compare the debug groups `AIVERSE Shadow Models`, `AIVERSE Opaque Models` and ICB command structures; screenshots support structural rather than timing analysis.
3. For debugger profiling, independently inspect available macOS Xcode crash diagnostics rather than assuming an engine defect:
```bash
xcodebuild -version
xcrun --find gpudebug || true
ls -lt ~/Library/Logs/DiagnosticReports/*Replayer* 2>/dev/null | head -10
```
4. If `gpudebug` is installed, run `xcrun gpudebug --help` to learn the actual CLI syntax for that Xcode; never guess flags or invoke a destructive command. It may support trace inspection without the failing GUI profiling flow.
5. If every performance replay crashes, file a Feedback report with Apple including Xcode/macOS version, system GPU, `.gputrace`, and crash report. Do not upload private captures publicly without evaluating their resource/shader content.

## Validation truth / next step

**Performed:** owner screenshot inspection; up-to-date Apple documentation/forum check; this handoff. **Not performed:** actual Xcode replayer debugging, crash report stacktrace analysis, native Metal frame capture repeat, new GPU model-pass timing or source patch. PR #39 stays Draft, Metal CPU MDI default remains OFF. Do not weaken explicit ICB buffer bindings, residency or GPU fences based on the replayer segmentation fault. Next agent should continue only after a command tree screenshot, `gpudebug` diagnostics or replayer crash stacktrace, or use supported Instruments GPU traces.
