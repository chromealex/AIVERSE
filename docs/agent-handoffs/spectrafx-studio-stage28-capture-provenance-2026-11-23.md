# SpectraFX Stage 28 — native capture scheduling provenance (2026-11-23)

Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Commits: a8bb7a65bbbc835121a20819e17ae81b7930f0c4, 5fc05329188cbfd48a481964377a53c28cacfd13, fd05f64f9b11269fafe3ff5f3a858e1b9d55522a, 85199d13d9b3f244a39de5fb054dae6b92cf45eb, 7237f712ab6e98b200b94a3a0410da7af143565b.
Not merged into main.

## Implemented
- apps/studio-native/StudioHost.h/.cpp: FxShot now retains Json provenance sampled on the native render-owner thread immediately before captureNextFrame scheduling, under its existing mutex. Status includes tick, generation, backend, camera, stage background and gpu flag; captureStatus returns the immutable provenance with the completed JPEG. The field sampling explicitly states render-owner-readback-request, not actual pixel submission/fence metadata.
- apps/studio-web/src/types.ts: optional typed capture.provenance.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: A/B slots retain native provenance in exported JSON reports. Existing request-time UI metadata remains separately identifiable.

## Tests actually executed
NONE. User explicitly requested feature-first implementation and deferred all tests. No exact-source AppleClang/Metal, TS/Vite, CTest or interactive UI acceptance has run.

## Limitations
- Native capture metadata represents the readback scheduling moment, not necessarily the exact presented frame. The callback may complete later; no native frame serial/fence identifier has been captured yet.
- No guaranteed identical camera, tick or particle state between A/B. No deterministic RNG seed API was added.
- A/B reports use lossy JPEG/sRGB and are not HDR/alpha/backend parity evidence.
- Capture lifecycle under viewport teardown, project switch, and callback race requires testing.

## Next steps
1. Audit GraphRuntime/CPU/GPU RNG and add explicit seed only if both backends can honor it.
2. Add exact frame serial and GPU fence completion metadata using actual RHI capabilities; avoid inventing frame identifiers.
3. Add atomic replay/capture state transaction and UI warnings for mismatched native metadata.
4. After all features, run focused native and Studio builds, GPU acceptance, tests and real screenshots. Update skill, AGENTS, roadmap and site only with validated claims.
