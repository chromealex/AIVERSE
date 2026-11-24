# Stage 56 — Particle VM birthRandom

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits f285e935914132381953a948ed9fbd1e1ec53f24, 2a6b65afac33bd9ca125c803ce39ac0adfbd40b9, 767f2fba67d07f6257304a989215638af458ba56, a2632f871ff3e8fc73be2065e7c33ca9df57a074, 13cc4d71ff3f874c06ad0b5cf2e3cdc42359e0c7.

SpectraParticleProgram.h adds BirthRandom read field with CPU UID hash; SpectraGpuProgram.h permits field 32 only with opt-in; SpectraGpuShaders.h reads stream 1; GraphRuntime and ComputeParticles propagate opt-in. Skill updated. No tests/builds. CPU/GPU semantics differ; no parity guarantee. Next Studio/MCP replay seed integration, checkpoint state and full feature QA. Audit writable field bounds and all enum consumers before release.
