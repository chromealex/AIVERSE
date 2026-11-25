# SpectraFX Stage 37 — native A/B provenance checks and agent skill (2026-11-23)

Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123; no merge to main.
Commits a4d8bd07e255686f6d18af0158410bc0f01c5ea8, 071600ace77b28fb71bacf129aca0294011a2b9b.

## Changed files
- apps/studio-web/src/panels/SpectraNativePreview.tsx: compares two native capture envelopes for tick, generation, backend, GPU mode, stage background and camera, plus missing/unsupported exact-frame identity. Shows warnings in Studio and exports nativeCaptureMismatch/frameExact=false in A/B report. Does not claim frame-accurate parity.
- skills/visuals/aiverse-spectrafx/SKILL.md: documents native replay/camera/seed/capture APIs, versioned presets, CPU versus Metal allocation seed limitations, readback provenance and the distinction between Studio-native MCP routing and legacy editor graph-v1 fx_graph_preview.

## Tests actually run
NONE. User explicitly requested all features before testing. No TypeScript build, native compiler, CTest, GPU QA or screenshot.

## Architecture / limitations
- The native Studio bridge and legacy EditorFxOps graph-v1 tool share a name but are not interchangeable. This stage documents the distinction; it does not modify MCP registration or claim universal MCP parity.
- frameExact=false remains: no verified captured-frame fence/serial is provided by gfx::Device::CaptureFn.
- GPU shader RNG and full CPU/GPU replay parity are not implemented. A/B pixel difference is lossy JPEG sRGB diagnostic only.
- Need actual TypeScript compilation and UI visual acceptance after feature completion.

## Next steps
1. Locate native MCP tool registration and add explicit replayPrepare/captureStatus/requireReplayReady schema where absent, without modifying legacy graph-v1 tool semantics.
2. Finish remaining Graph v2 editor data interfaces and node authoring.
3. Add exact captured-frame metadata in RHI callback when backend support exists.
4. Run all tests only after feature completion; update devlog with genuine screenshots.
