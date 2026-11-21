# SpectraFX Stage 29 — guarded recovery from GPU checkpoint into CPU ParticleSystem

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Stage started at HEAD `2adc7b398439fc84718a975537edd3a4a8f0500e`.

## Exact source commits
- `5641b3faf600a1785f1c77028ea5740d3d7dd9d5` — `engine/fx/ParticleSystem.h` adds `FxImportedParticle` and `ParticleSystem::importLiveParticles`.
- `2d8f0968521a4b29a466a4291185f8608498ba2b` — `engine/fx/ParticleSystem.cpp` validates and transactionally appends live CPU particle records without spawning new emitter instances or rerunning Spawn VM.
- `54e36c48425d893a2e203658bb2fd2a5845a34a5` — `engine/fx/SpectraGpuParticles.h` retains most recent complete checkpoint/frame revision and `ComputeStats::recoveredOnFallback`.
- `d340ccb95a1ff2c667eb3bb27ab83cb2d7cdc426` — `engine/fx/SpectraGpuParticles.cpp` stores successful checkpoint captures; on `switchToCpu` conditionally restores from current-revision full checkpoint, otherwise counts and reports losses.
- `868b4d91a160a1834bd74a9a22b11274a1b97986` — `docs/spectrafx.md` public contract, warnings and usage.

## Import and restore API
```cpp
std::vector<aiverse::fx::FxImportedParticle> records;
// All records must be finite and 0 <= age < lifetime,
// effect must already exist and be compatible.
bool ok=cpuParticles.importLiveParticles(effectId,records);

// GPU runtime: host explicitly captures a completed-frame checkpoint.
// Internally retained checkpoint will be consulted only if GPU fallback
// occurs before the next GPU frame submission.
aiverse::spectra::GpuCheckpoint checkpoint;
bool captured=graphRuntime.captureGpuCheckpoint(checkpoint);
```

The CPU import validates the entire batch, effect and quality-tier budget before appending any record. Imported particle data includes position, velocity, age, total lifetime, size, color, 8 instance VM inputs and 8 custom attributes. Renderer batching uses the existing emitter runtime batch. The particle has no emitting instance; its state advances via ordinary CPU Update and renders with the existing CPU renderer. CPU spawn is NOT replayed, preserving current custom VM attributes.

Recovery guard requires (1) complete stored checkpoint, (2) exact `vmFrameRevision_` match, (3) no unsubmitted GPU births, and (4) matching physical slot counts. Only live physical GPU entries are imported; a missing or stale capture cannot be presented as successful state migration. In fallback `recoveredOnFallback` records imported particles, `lostOnFallback` records remaining allocated GPU particles. For GPU-required mode, the existing strict no-CPU-fallback rule remains in force.

## Limitations and correctness risks
- **NOT automatic periodic checkpoint capture.** The current Metal RHI sync readback blocks. Caller must capture at a safe completed-frame boundary. Once GPU is unhealthy, no guaranteed capture is possible. On the next GPU step the previous checkpoint becomes stale.
- **NOT a provably lossless simulation handoff.** CPU and GPU integrators/VM numerical paths are not yet proven equivalent; no continuation of emitter source handles, original RNG or timed future bursts. The importer intentionally restricts complex renderers/events/materials. Do not promise parity or absence of an animation jump.
- Checkpoint frame revision is presentation-only; it never enters deterministic ECS or network rollback. No genre-specific presentation code was introduced.
- GPU load may fail and CPU backend initialization may also fail; the API reports these states. No silent success claim is appropriate.
- Strict budget: if restored particles exceed the CPU quality-tier maximum, import fails atomically and previously-existing CPU particles are left untouched.
- A retained checkpoint is currently scoped to the ComputeParticles instance and cleared at shutdown/recovery; host save/load persistence is out of scope.

## Actual verification
Successful GitHub connector source writes only. **NOT RUN:** native CMake/Clang compilation, Metal compiler, CTest, GPU/CPU parity, device-loss fault injection, editor startup, GUI screenshot, frame-fence audit or performance benchmark. All source behavior remains unverified.

## Next Stage 30
1. Implement safe nonblocking *periodic* checkpoints for live GPU state with budget/cadence, frame-token and callback-cancellation discipline. Do not use blocking 3-buffer reads every game frame.
2. Define/verify an accurate per-frame recovery freshness model, including pending birth buffers, dropped/died particles and GPU fence completion. Do not automatically import stale snapshots.
3. Extend CPU import for source identity where needed, or explicitly constrain recovery to non-looping/simple sprites; add full restore validation and smoke tests.
4. Compile and execute CTest/Metal device smoke once the user changes the features-first testing policy. Flag real compile errors and fix them before production merging.
5. Continue mandatory per-stage handoffs with exact files, SHAs, actual tested status, current branch, known limits and next work. Avoid force push and concurrent-agent overwrites.
