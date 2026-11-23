# SpectraFX Stage 34 — native replay completion and capture guard (2026-11-23)

Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123; not merged into main.
Commits e0c122f6378ef7b6ed162410f8c09336b23a1f85, 6eb4fe0d69395a35819821d1fa6315f238991470, 1ba7876befcb37e9408d751b8d8b717d74655c67, 498f760d81b33b560482830eb0b623819d2af22e.

## Implementation
- apps/studio-native/StudioSpectraPreview.h: replayPrepared_ tracks a successful replayPrepare; status.replayReady only true when preview is paused/healthy, target tick reached, no pending restart/initial burst/GPU seed/clear and no queued scalar updates. Explicit play, restart, seek, step, clear, load and stop invalidate replay completion.
- apps/studio-native/StudioHost.cpp: capture optionally accepts requireReplayReady boolean; native status is sampled on render-owner thread before scheduling readback, and nonready replay captures are rejected.
- apps/studio-web/src/types.ts: replayReady and requireReplayReady fields.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: wait for native replayReady before reporting restore convergence; use guarded capture when current replay preset and ready status are present.

## Tests actually run
NONE, per explicit user instruction. No compilation, CTest, TS/Vite, GPU QA or editor screenshot.

## Important limitations
- The guard checks status immediately before scheduling capture, but is NOT an atomic GPU frame fence. It cannot prove the captured pixels correspond exactly to the reported tick. Frame serial and RHI fence metadata are still needed.
- GPU Shader RNG is NOT implemented. Metal slot allocation seed is not shader RNG. GPU VM eligibility rejects several random effects; unsupported seeded shader behavior must not be silently advertised.
- The host's status check and captureNextFrame registration use separate render-owner dispatches. A render frame may occur between them; an actual atomic transaction is still needed.
- ReplayReady is a preview scheduling barrier, not CPU/GPU visual parity or a complete particle snapshot.
- Full Graph Editor, MCP/skills and cross-platform parity remain future work.

## Next
Implement atomic render-owner capture request and exact frame provenance if RHI permits, then finish remaining editor/MCP/skill features. Only after all features are complete, run comprehensive exact-SHA tests and hardware QA.
