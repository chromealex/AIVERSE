# Stage 57 — GPU shader replay seed

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits fd36bde1f5a4e2d06f8482535df9b4840925188b, acc189c3b77a1b5c0ba2702897b5595e27032398, 78170862682ea024926ce0f184c52a7e011e0f69, d0285afe6d7f297f0b22b3e9f2862c4edb69439b, 6ed4f11ab6af9e44ae3f9d589d3b8364ad7cf1ab.

StudioSpectraPreview.h adds load gpuBirthRandom opt-in and deferred replay shader seed reset using existing retry gate. types.ts, spectraReplayPreset.ts and SpectraNativePreview.tsx add typed seed, validation, preset serialization and field. Workspace.cpp MCP schema exposes gpuShaderSeed. Skill updated. No tests/builds. UI load opt-in toggle and preset opt-in restoration still pending; shader seed alone does not change eligibility. Next finish UI opt-in, checkpoint RNG state and capture provenance before QA.
