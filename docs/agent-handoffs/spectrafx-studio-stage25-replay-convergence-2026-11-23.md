# SpectraFX Studio Stage 25 — replay convergence and scalar ACK (2026-11-23)

## Provenance
Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123.
Implementation commit f586af7d6a33d3c7f3d53f0605b3072ff4d845f6. No main merge.

## Changes
File: apps/studio-web/src/panels/SpectraNativePreview.tsx.
- Replay restore now pauses, restores stage/playback/camera, requests native restart, reapplies tracked scalar parameters, and requests seek.
- The native seek is asynchronous (StudioSpectraPreview::kSeekTicksPerFrame=8); poll actual native status until tick matches, abort on backend error, overshoot or 16-second deadline. UI marks replay restoration in progress.
- Live scalar tracking is updated only after a native ready/error-free parameter command acknowledgement, instead of after a generic command helper that silently returned an error status.
- No unsupported random seed or GPU particle snapshot API was invented.

## Tests actually run
NONE. No exact-source npm typecheck/Vite, C++ build, Metal/Vulkan runtime or UI QA was executed. Implementation unverified.

## Known risks
- A native parameter ACK means queued application, not confirmed GPU execution. Parameters queued before restart/seek may not survive all backend transitions; verify on hardware.
- Polling status via independent IPC can race normal 500ms UI polling; serialized capture/restore needs a native transaction for stronger guarantees.
- 16s timeout is a UI deadline, not deterministic performance guarantee. Render loop must continue while seek is pending.
- No deterministic random seed, capture fence provenance, exact camera snapshot on GPU frame, persisted particle state, or full replay equivalence. Stage 24 parser and UI need TS compilation.

## Next steps
1. Run npm typecheck, test:graphs, build and native Studio focused CTests on exact SHA; fix errors.
2. Add native transaction for camera/tick/parameter preset, native completion status, and GPU capture metadata on render owner.
3. Audit GraphRuntime RNG seeding and expose seed only after proving both CPU and GPU implementations support it.
4. Add regression tests for seek convergence, rejected scalar and replay error paths.
5. Update skills, AGENTS and roadmap after validated implementation; real screenshot required before devlog.
