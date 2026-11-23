# SpectraFX Stage 30 — explicit CPU replay seed (2026-11-23)

Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123. Not merged to main.
Implementation commits: 9b1ccd2c2b87cfa4ead2f47d73efbe692040fc7f, 7a51aa773062f64fcfbd6577948b6329f8bef5ec, 846aa36ddf9cc56a15271b1a953aa193f5907920, 42f60c326c6c28921308aac3a24d029440c6dd48, d76476d5c038a9ad68c420e36977d18b58693adc, ebf2bb7ff56315e818de44b40a4b4b9dc7c66343, ed02e4a81ba12fca2e70f0a0c4348028a26bc0a5, 9903c9bec9768c0602bd6f2df41a6cfa8ef20a05.

## Implemented
- engine/fx/ParticleSystem.h/.cpp: resetDeterministicSeed(uint32_t) clears particles and resets xorshift32 RNG state and nextUid; zero maps to nonzero default xorshift seed.
- engine/fx/SpectraGraphRuntime.h/.cpp: resetDeterministicSeed(uint32_t) forwards to dedicated CPU ParticleSystem, rejects GPU wrapper and shared BatchWorld.
- apps/studio-native/StudioSpectraPreview.h: replayPrepare optionally accepts bounded integer seed 0..2147483647, refuses GPU backend and requests CPU reset before scheduling burst.
- apps/studio-web/src/types.ts, panels/spectraReplayPreset.ts, panels/SpectraNativePreview.tsx: optional seed bridge, bounded JSON parser and Studio UI seed field/import/export.

## Tests actually executed
NONE. User explicitly requested all features before testing. No C++ compile, TS typecheck, CTest or GPU QA was run.

## Known limitations
- CPU-only seed is not GPU/CPU equivalence. GPU VM seed buffers need separate implementation and validation; seeded GPU replay rejects rather than silently approximating.
- Seeded reset clears all live particles in the dedicated CPU instance and resets particle UIDs. It is not safe as a non-destructive mid-frame reseed.
- Default RNG state is nonzero; seed=0 maps to default. Full replay still depends on exact graph, inputs, frame tick and host state.
- Stage 27 deferred burst and queued parameters still need native QA. Imported scalar values are browser-authoring snapshots, not complete runtime state.
- No exact GPU readback fence provenance or atomic stage/camera/seed transaction.

## Next steps
1. Finish GPU RNG/VM seed ownership audit and implement supported GPU seed API without assuming CPU equivalence.
2. Add native capture frame serial/fence provenance and atomic replay/capture.
3. Complete Graph editor and MCP parity, skills and roadmap.
4. Only after features are finished, run full focused native and Studio acceptance; fix all issues then.
