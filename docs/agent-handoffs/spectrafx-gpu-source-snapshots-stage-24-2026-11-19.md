# AIVERSE SpectraFX Stage 24 — generation-safe per-particle GPU VM snapshots

Date: 2026-11-19. Branch: `feature/universal-entity-inspector-fps`. Starting HEAD: `824e4741be4cffdba38f06fb764a3729a7719e56`.

## Completed implementation commits
- `5aa62f4e2151f8adff5bfe5c391341cc09b28700`: `engine/fx/SpectraGpuSourceParameters.h` presentation-only `gpu_program::SourceParameters` with source slot + monotonic generation, initial 8 floats, per-particle snapshots, future-vs-existing mutation policy, kill/retire, bounds, finite-value checks, non-wrapping generation exhaustion and revision.
- `d2f5fd7681807da6c45930834bf7c8cc41bb1db6`: `engine/fx/SpectraGpuProgramShaders.h` optional per-particle parameters via 48-byte snapshot buffer at Metal compute binding 4, selected by dispatch.reserved=1, retaining uniform param mode 0.
- `f2000c79deaf31cbde1223f65328b5806ed3c6b7`: `engine/fx/SpectraGpuVmDispatch.h` `init(device,payload,maxParticleSlots=4096)` and `dispatchSources(states,count,stage,dt,registry)`, allocating/updating the extra storage buffer with bounded physical slot ranges and explicit teardown.
- `2e4fc0c23d80041482c174fcda9f9de2b58314a4`: exported `SpectraGpuSourceParameters.h` via public `engine/fx/SpectraFX.h`.
- `30ed8d7ee6fbff1f8453c5220194b6d71498690f`: `docs/spectrafx.md` authored API and limitations.

## API semantics
```cpp
using namespace aiverse::spectra::gpu_program;
SourceParameters registry(256,4096);
auto a=registry.create({1.f,2.f,0,0,0,0,0,0});
auto b=registry.create({7.f,8.f,0,0,0,0,0,0});
registry.spawn(0,a);registry.spawn(1,b);
registry.set(a,0,4.f,false); // only future births from a
registry.set(b,1,9.f,true);  // current b particles too
registry.retire(a);          // a no longer spawns or mutates
registry.kill(0);            // release logical particle slot
// MetalVmPass pass; pass.init(device,payload,4096);
// pass.dispatchSources(stateBuffer,2,program::Stage::Update,dt,registry);
```

Particle snapshots are `alignas(16)`, 48-byte values[8], source slot, generation, active and reserved. They do not modify the 64-byte GPU particle state or material 40-byte vertex layout. Invalid/stale source IDs are rejected; retiring does not rewrite live particles; slot reuse uses a different generation and cannot accidentally mutate an older generation's live particles. Bounds of source and particle slots are explicit. No gameplay ECS writes.

## Verification performed
GitHub source writes succeeded, but **NO compiler, CMake, CTest, Metal pipeline compilation, device smoke, shader correctness test, GPU readback, visual parity or performance benchmark was run** (user previously requested features before tests). All results must be treated as unverified.

## Critical limitations
1. Experimental `MetalVmPass` only. **No GraphRuntime or ComputeParticles integration**, automatic GPU selection, complete Niagara parity, rollback state, per-instance authoring UI, or production renderer contract.
2. The host must mirror each physical particle slot's lifetime exactly: call `spawn` after assignment and `kill` on recycle. A retired source's previously live snapshots intentionally persist until those particles die.
3. Per-particle snapshot prefix is uploaded every dispatch; no GPU delta upload, ring buffering or guaranteed independent repeated dispatches in one Metal frame. Metal RHI update/upload ordering and resource lifetime must be verified before cross-emitter production use.
4. The GPU error counter is not automatically read; Stage 23 kernel's transactional error semantics differ from CPU VM's per-statement skip policy. GPU failure cannot claim seamless live-effect fallback.
5. Inputs are bounded/finite at the C++ API, but shader stage 23+24 is uncompiled and could have MSL compilation/layout issues. Do not relax existing CPU fallback guard.
6. Existing full effect serialization gate in `inspectGraphGpuEligibility` rejects VM effects; that is intentional until rigorous parity is verified.

## Next bounded Stage 25 (functional first)
- Implement explicit synchronization and state ownership between source lifecycle and GPU physical slot allocator; avoid publishing a physical particle to GPU without its generation-safe snapshot. Scheduler should run Spawn post-seed and Update post-integration/pre-render without altering gameplay tick ownership.
- Add per-source GPU snapshot dirty-range uploads or frame-ring scratch to avoid replacing in-flight Metal buffer contents; confirm barriers and safe update semantics with the RHI.
- Add GPU error-counter readback/diagnostics + safe capability gates; define a *real* fallback policy that preserves or explicitly reports loss of GPU-resident particles.
- Add a complete supported/rejected CPU/Metal VM parity contract, prepare future unit tests, and only then connect opt-in GraphRuntime. Preserve existing `RequireGpu` rejection for noneligible VM graphs.
- Every stage must commit handoff with exact files, SHAs, tests actually run, limitations and concrete next step.
