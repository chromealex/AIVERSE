# SpectraFX Studio Stage 22 — native FX camera snapshot and restore (2026-11-23)

## Provenance
Branch: feature/spectrafx-html5-data-interfaces-20261123; no merge to main.
Commits: 5a8611f3f19bcd636b1c50df56be959e88384185, 80cc62dab7403c55f4732bccade49835025e763b, bb37ac4d43f7deaaf67684acf73638edf422ff7d, 33ecafc8c2a681fa3468edb061766ea41cd5a126.

## Changes and API
- apps/studio-native/StudioScene.h: FX `status` and `cameraPreset` return `camera` object (yaw,pitch,distance,targetX/Y/Z,ortho). `cameraRestore` accepts validated bounded finite fields, rejects non-FX stage, and updates only the dedicated FX camera. Values are rounded by existing Json::number(double) serialization.
- apps/studio-web/src/types.ts: typed `StudioSpectraCameraSnapshot`, optional state.camera and `cameraRestore` bridge action.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: Lock camera / Restore camera controls; capture request metadata includes actual status camera fields instead of placeholder. Mismatched camera snapshots warn during A/B comparisons.
- No new GPU readback, gameplay writes or Scene camera modifications. The native Scene/Fx viewport separation remains.

## Tests actually run
NONE. No local native compiler, Metal/Vulkan device, npm typecheck or browser QA. Do not claim deterministic replay or verified correctness.

## Important limitations
- This is camera pose restoration only, NOT deterministic full particle simulation. Different ticks, random seeds, emitter state, post effects and in-flight GPU frames can differ.
- `camera` on capture metadata reflects latest polled UI status at capture request, not guaranteed exact GPU completion pose; async render-owner and UI can race.
- Camera values use Json rounded numeric encoding; camera projection and target require visual parity QA.
- Existing `cameraReset` and `cameraOrbit` bridge routing must be checked against the dedicated FX scene.
- Potential C++ header include availability of std::isfinite/abs must be validated on AppleClang/Windows.
- Camera snapshot mismatch uses JSON string equality; float quantization and capture status timing may yield false positives.

## Next
1. Compile/typecheck exact SHA; fix any C++ Json/TS issues. Validate native Metal UI and compare A/B camera frame before and after restore.
2. Add a native atomic capture transaction that freezes frame tick and camera on render owner and records exact capture provenance.
3. Extend replay preset to include stage background, graph revision, tick, parameter values and deterministic seed where supported.
4. Add per-image draggable split handle and unequal-dimension guard.
5. Add MCP controls and skill docs after verified native behavior; update devlog with real Studio screenshot only after QA.
