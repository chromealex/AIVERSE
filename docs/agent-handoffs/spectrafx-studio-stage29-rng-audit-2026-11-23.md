# SpectraFX Stage 29 — RNG backend audit and replay scheduling (2026-11-23)

Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Implementation: 6e77eca6bf4f726d03154615d7d26ea916b831ff
Not merged into main.

## Code
apps/studio-native/StudioSpectraPreview.h: needsFrame() now includes restartBurstPending_. A rejected scalar or deferred initial burst must not strand the native render loop after pendingRestart_ is cleared. No gameplay state modified.

## Actual RNG architecture findings
- engine/fx/FxInternal.h has per-ParticleSystem Rng, xorshift32 with default state 0x9E3779B9, and prand(uid,salt) for stable per-particle values.
- engine/fx/ParticleSystem.cpp consumes rng for spawn rates, curves, random direction and sub-emitter events.
- engine/fx/SpectraGraphRuntime.h exposes no seed setter or guaranteed backend-independent seed contract.
- engine/fx/SpectraGraphRuntime.cpp rejects random ranges, curves and spread for the restricted GPU v1 eligibility path; supported GPU VM and fallback paths need separate review.
- engine/fx/SpectraGpuParticles.h has GPU seed buffers, but their semantics and equivalence to CPU RNG were not established.
- Therefore Stage 29 does NOT claim seeded replay implementation. Adding a public seed now without proving CPU/GPU semantics would be misleading.

## Tests actually run
NONE, per user instruction to finish features first. No compiler, CTest, native device or UI execution.

## Limitations
The CPU RNG has a deterministic default state, but runtime reuse, particle UID progression, random draws and GPU implementation may differ. A repeatable initial RNG state is not equivalent to exact cross-backend particle replay. The current Stage 26-28 preview and capture features remain unverified.

## Next engineering step
1. Trace ParticleSystem::Impl::rng initialization/reset and particle UID lifecycle; add explicit bounded CPU seed setter with reset semantics.
2. Trace GPU VM seeds and GPU burst scheduling; implement a separate verified seed contract or reject unsupported seeded GPU replay explicitly.
3. Thread supported seed through GraphRuntime, native Studio preview, JSON replay and MCP without silent fallback.
4. Continue feature work without tests; perform full exact-SHA acceptance only after user authorizes testing.
