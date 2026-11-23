# SpectraFX Stage 31 — seeded Metal slot allocation (2026-11-23)

Repository chromealex/AIVERSE. Branch feature/spectrafx-html5-data-interfaces-20261123. Not merged into main.
Implementation commits: 7169073d038cbdf8ffb35bffec204891df5ecb8f, 07b37999054af16f1bc88cf5a437d397005647be, 572f9c0f8acffc927b6052f691ec586cef17401d, d95edd697f39f31bd4f3f11d7c3589ec6b9fb8aa, 7bd5ee37a84cc76d91739f65b89ac0e7ae1c99ee, f61c7579af484d1012b06866a91f05f0c5fabc09, 8d9d526dec890123fd6f2929ed66c2b2c3d336cc, 9860f2f301b6c33aa1cf13260bb758156b0a00a1.

## Implemented
- engine/fx/SpectraGpuParticles.h/.cpp: resetGpuAllocationSeed(uint32_t) on Metal-only active ComputeParticles. Calls checked tryClear, then performs bounded deterministic Fisher-Yates permutation of freeSlots_ with xorshift32. Changes slot assignment ordering, NOT shader RNG or particle spawn distribution.
- engine/fx/SpectraGraphRuntime.h/.cpp: resetGpuAllocationSeed forwards to dedicated Metal wrapper and explicitly rejects CPU/shared batch paths.
- apps/studio-native/StudioSpectraPreview.h: replayPrepare accepts gpuAllocationSeed separately from CPU seed, rejects unsupported backend.
- apps/studio-web/src/types.ts and panels/spectraReplayPreset.ts/SpectraNativePreview.tsx: optional Metal allocation seed in typed bridge, versioned JSON, UI. CPU seed and GPU allocation seed mutually exclusive in JSON validation and UI save.

## Tests actually executed
NONE. User requested feature-first work. No compilation, TypeScript typecheck, CTest, Metal execution, or manual UI QA.

## Limits and known hazards
- This is NOT GPU random distribution seeding and NOT CPU/GPU replay parity. Metal supported path does not expose equivalent xorshift particle RNG. Do not label it as full GPU RNG Seed.
- GPU reset requires a healthy device, open frame and no active render/compute encoder. replayPrepare is called between frames; tryClear may reject when no frame is open. The API returns an explicit error. A deferred render-owner reset is needed for robust support.
- Stage 26 replayPrepare validates scalar table, but CPU/GPU seed modes should be checked before any state mutation; GPU seed failure currently does not mutate replay tick but may clear GPU particle state if tryClear partially fails.
- Fisher-Yates order is reproducible only with equal free-slot capacity and identical subsequent burst scheduling. Slot permutation affects execution order, not guaranteed visual pixels.
- Cross-backend shader RNG, GPU frame serial/fence provenance and atomic replay remain outstanding.

## Next steps
1. Move GPU reset/seed application to safe inside-frame prepare() with pending request state, preserving failure/retry.
2. Trace Metal shader VM PRNG and add explicit GPU particle random seed only with verified semantics.
3. Complete atomic replay/capture, graph editor and MCP/skill parity.
4. After all features, run exact-SHA builds and hardware QA, then update devlog with real screenshots.
