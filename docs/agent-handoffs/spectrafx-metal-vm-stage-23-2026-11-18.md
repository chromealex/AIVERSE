# SpectraFX Stage 23 — experimental Metal VM kernel + dispatch adapter
Date: 2026-11-18
Working branch: `feature/universal-entity-inspector-fps`
Stage 22 predecessor: `419aade9e2ebb8a04f2dc1330cb69c147b1920e2`

## Commits
- `2604900c1a915f4d575d75a5a7eddf407edf179f` — `engine/fx/SpectraGpuProgramShaders.h` independent Metal kernel
- `439a024ca7ac681f8cda8522cddc103b45c3d3ac` — `engine/fx/SpectraGpuVmDispatch.h` standalone resource-owned pass
- `078a3c50e01f3192e20a35bc47e74e8254af14df` — correct canonical RHI header
- `171ef4e4d247a96966929fd29667c19a00657737` — `engine/fx/SpectraFX.h` public opt-in include

## Implemented
An isolated Stage-22 ABI interpreter kernel named `cs_main` reads a GPU-side `Payload`, float8 per-dispatch parameter buffer, existing 64-byte particle state and dt, and executes the conservative Spawn/Update expression subset (constant/read, arithmetic, saturate, clamp, lerp). It validates payload version/counts and instruction stack/bounds on the GPU. Unsupported/invalid execution increments a GPU atomic error counter and does not publish a partially updated particle. This is a source-level kernel implementation, not validated GPU correctness.

The public header-only `gpu_program::MetalVmPass` offers `init(Device*,Payload)`, `dispatch(stateBuffer,count,stage,dt,params)`, `shutdown()`, `ready()`, `reason()` and `errorCounter()`. The pass owns only its compute pipeline, program, parameter and error buffers. The host owns the device and particle-state storage, must not destroy either during the pass, and must dispatch in-frame outside an open render/compute encoder. The input state layout matches `ComputeParticles::ParticleState` (positionAge/velocityLife/tint/size, 64 bytes). Only Metal compute is allowed.

### Unchanged production safety
- **NOT connected to GraphRuntime, ComputeParticles::step or default GPU backend selection.** Advanced VFX still run entirely on CPU; existing simple GPU billboard path remains unchanged.
- This stage does **not** provide per-source generation identifiers, multi-instance parameter mapping, GPU-to-CPU migration or GPU error readback. A uniform float8 buffer applies to one dispatch, not to independent live instances.
- The shader does not manage emitter state, CPU prewarm, live source edits, GPU compaction, GPU draw arguments, collision/trigger/death, advanced renderer bindings or cpu-effect cross-emitter scheduling.
- No FPS/RTS/genre dependency, authoritative World write or PRISMA post-processing ownership.
- Existing graph asset formats, material semantics and vertex layout are unchanged.

## Verification and caveats
**NOT RUN:** native compilation, Metal shader compiler, CMake, CTest, CI, GPU device smoke, GPU error-counter inspection, FPS/RTS visual integration or performance tests. The GitHub connector confirmed source writes, NOT runtime correctness. The standalone Metal API is experimental and must not be characterized as production ready.

Important parity note: CPU VM continues processing later statements when a particular evaluation fails; this experimental Metal implementation rejects the whole particle update on evaluation failure. Restrict GPU eligibility to verified parity-safe programs or implement matching error semantics before any automatic opt-in. The VM does not yet preserve continuous-source identity or existing-particle parameter updates. Never silently activate it.

## Next functional Stage 24 (required before production switch)
1. Add per-particle source generation/index and per-source 8-scalar snapshot buffers; implement atomic source parameter updates for future births and optional existing particles. Ensure no stale generations or cross-source contamination.
2. Schedule Spawn VM after spawn seed upload, Update VM after existing integration and before collision/render, with guaranteed Metal RHI barriers. Compare against exact CPU VM time-order and life/death semantics; preserve prewarm.
3. Add RHI error-counter readback or GPU diagnostic instrumentation, and explicit error policy. Introduce strict graph eligibility only after the subset is bitwise/visually validated and live-effect fallback is defined. Current GPU fallback discards live particles: not seamless!
4. Add functional tests: payload ABI sizes, generated opcode subset/rejections, per-source isolation, edit semantics, invalid data, empty population, load/compile errors, Metal smoke, GPU vs CPU parity and rollback unaffected. User explicitly deferred test **execution**; do not report passes.
5. Consider per-stage bounded commits + handoff before further changes; check for concurrent updates to `main` and other agents' branch without overwriting them.
