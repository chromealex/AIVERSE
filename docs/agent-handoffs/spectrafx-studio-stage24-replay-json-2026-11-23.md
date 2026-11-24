# SpectraFX Studio Stage 24 — Replay JSON import/export and scalar snapshots

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Implementation commits: b85e58a5004566510a7af179d17466250e5bd705, af12413f749e96a588f485d13c35c9fad932a7f1.
Not merged into main.

## Implemented
- apps/studio-web/src/panels/spectraReplayPreset.ts: versioned aiverse.spectrafx.replay JSON schema, strict bounded parser (64 KiB, max 8 finite scalars, camera finite ranges, known speed/stage values).
- apps/studio-web/src/panels/SpectraNativePreview.tsx: Save Replay Preset includes tracked live scalar values, Export preset as JSON, Import preset from local JSON with project/graph identity and parameter schema/range validation. Restore reuses serialized native commands, then applies scalar values.
- No arbitrary file write into project; browser download only. No engine ECS mutation. Presets are local to UI and project switching clears them.

## Tests actually executed
NONE: exact-source TypeScript/Vite build, Mac native Studio, GPU, UI and CTest remain unrun. Do not claim this is QA-passed.

## Known limitations
- Tracked scalars include values applied via this panel only, not authoritative current native values; the panel currently updates its scalar tracking after command returns even if native command reports an error. Fix by checking result.error/ready before recording.
- JSON graphSignature is full serialized graph; 64 KiB cap may reject large graphs. Consider hash + canonical asset GUID instead of full graph string.
- Replay is not deterministic particle simulation: no seed, exact GPU frame fence, runtime particle snapshot or post-process state.
- Imported file input must be verified in WKWebView/WebView2, including disabled behavior and stale project changes during async file read.
- Native camera restore from Stage 22 and current TS changes need compilation. Split comparison still needs dimensions guard.

## Next steps
1. Run npm typecheck/build and focused native CTest on exact SHA, fix errors.
2. Only track scalar values after successful native parameter command; ensure restore reports failures.
3. Add canonical graph GUID/revision digest and bounded graph-independent preset identifiers.
4. Implement native deterministic seed/restart and atomic render-owner capture provenance; then Metal/Vulkan acceptance.
5. Update skills, roadmap and screenshot devlog only with actual verified evidence.
