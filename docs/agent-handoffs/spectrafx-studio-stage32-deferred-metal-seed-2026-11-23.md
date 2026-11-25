# SpectraFX Stage 32 — deferred Metal allocation seed (2026-11-23)

## Provenance
Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123, not merged into main.
Implementation commits: 119289dd047e72bf3b0d2e8ba27d88b76ffc8468, 377c9fe38c95b6adf685cda12002c05777a687fd, 93a513035d463bd87efbdb30441d3c2f6aebcaab, 8cdf35ac72ea6d43646c97baf7f07b62d1ea91bc, a89c233a105b9e059c502cddefad97261cc96bcd.

## Changed files
- apps/studio-native/StudioSpectraPreview.h: replayPrepare now validates GPU allocation seed and queues it rather than immediately calling Metal GPU reset between frames. prepare() executes the checked reset on the native render owner within an open frame, retains pending seed on failure, clears error after success, and avoids redundant clear. needsFrame() includes pending seed. status includes gpuSeedPending. Load, explicit restart, clear and stop cancel stale pending seed. Reject simultaneous CPU and GPU seeds.
- apps/studio-web/src/types.ts: gpuSeedPending optional typed state.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: replay settle now requires target tick, no pending GPU seed and no clear.

## Tests actually executed
NONE. User explicitly deferred testing until feature completion. No AppleClang/Metal build, Vite typecheck, CTest or interactive Studio QA.

## Limitations
- This is deterministic Metal free-slot allocation only, not GPU shader RNG, and not CPU/GPU visual parity.
- A render-owner frame must be active for tryClear; the deferred approach avoids unsafe immediate IPC reset but has not been validated against actual frame lifecycle.
- Replay preparation still modifies stage/camera/playback via separate commands and does not atomically bind capture to a completed frame.
- GPU reset errors can be transient or persistent; pending requests retry each native frame until superseded or viewport closes. No retry budget yet.
- Existing Stage 30 CPU RNG and Stage 31 Metal allocation seed code remain uncompiled.

## Next steps
1. Finish GPU shader random seed semantics, if supported by the Metal VM, without mislabeling allocation seed as full RNG.
2. Add bounded retry budget / explicit terminal status for persistent GPU reset failure.
3. Add native frame serial/fence provenance and atomic replay/capture request.
4. Finish Graph Editor and MCP parity; update skills/roadmap.
5. Only after all features are complete, run full native and web QA and record genuine screenshots.
