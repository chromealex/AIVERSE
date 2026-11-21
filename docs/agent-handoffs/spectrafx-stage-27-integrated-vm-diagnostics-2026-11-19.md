# AIVERSE SpectraFX — Stage 27: integrated VM diagnostics and non-lossless fallback accounting

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting HEAD observed: `19f34a3fb7d60b45652ca73e5f5e8b0470bdb4a9`

## Actual implementation commits
- `c6e7105e55665ce850ad63185ad7defb8f560f7c` — `engine/fx/SpectraGpuShaders.h`: record invalid VM stages, failed expression evaluations, invalid actions and non-finite results using Metal atomic counter at compute buffer 12. Continue skipping a failed statement, retaining the CPU VM's per-statement failure model.
- `0bb4abfc3262b6abdd93727c4e4d53f508a9707b` — `engine/fx/SpectraGpuParticles.h`: `ComputeStats::gpuVmErrorsLastRead`, `gpuVmErrorsKnown`, `ComputeParticles::readGpuVmErrors`, and `vmErrors_` RHI handle.
- `eb33627c3c1bf725814df0d918d88dddaf915cd7` — `engine/fx/SpectraGpuParticles.cpp`: allocate, bind, reset, destroy GPU VM error buffer; read back diagnostics through existing `gfx::Device::readBuffer`; invalidate freshness on new frame/CPU fallback; account for pending as well as previously drawn GPU particle allocations in `lostOnFallback`.
- `105b15e47d236e74a0e25523fe1f9037f7fc46fc` — `engine/fx/SpectraGraphRuntime.h`: expose diagnostic readback through public graph host API.
- `99659944503bf35e167e662bdfe3c24da389a175` — `docs/spectrafx.md`: public semantic and recovery limitations.

## Architecture and API
`bool GraphRuntime::readGpuVmErrors(uint32_t& count)` and `ComputeParticles::readGpuVmErrors` are diagnostic-only synchronous reads. A successful call after the last GPU frame has completed sets `gpuVmErrorsKnown=true`; every new Metal frame resets the counter and invalidates freshness. Failed readback returns false and leaves unknown status; do not interpret stale `gpuVmErrorsLastRead` as an up-to-date zero. The integration uses existing Metal buffer slots 0..11 and adds 12, avoiding another emitter implementation or genre dependencies.

The kernel counts invalid VM statements while preserving the shader's semantics (skip only invalid statement; keep others). The existing CPU `ParticleProgram` remains the authoritative compatibility reference, and no new program has been made GPU-eligible. `SpectraGpuFramePlan` continues to control Spawn → Integrate/Update → Compact/Gather → Draw.

`ComputeParticles::switchToCpu` in permissive modes still discards GPU-resident particles. Pending allocations are counted in `lostOnFallback` rather than only the last frame's draw estimate. `RequireGpu` still fails instead of silently switching; the fallback is **explicitly not lossless**.

## Verification evidence
GitHub connector confirmed the five source/documentation writes. No native checkout/build was performed in this stage. **NOT RUN:** C++ compiler, Metal shader compilation, CMake, CTest, GPU error readback on device, fault injection, simulated device failure, screenshots or CPU/GPU numerical parity.

## Risk and next bounded stage (28)
1. Implement a real no-loss recovery path: periodic complete GPU/CPU checkpoint (position/age/velocity/tint/size, live params and custom attributes), safe frame fence/readback, and CPU particle import API; migrate all live particles atomically. Until available, report the current loss explicitly and do not imply seamless fallback.
2. Prefer asynchronous `Device::readBufferAsync` with callback lifetime/generation checking and completed-frame accumulation. Verify whether Metal permits buffer slot 12 on target devices, and validate RHI error paths; a blocking diagnostic call is unsuitable per-frame.
3. Add compile and device checks for this stage, plus source tests for invalid VM statement counters, reset semantics, readback failures, fault-injection loss accounting, `RequireGpu` rejection and fallback diagnostics. User requested feature development before running tests, so record tests as NOT RUN.
4. Keep SpectraFX presentation-only and shared among game genres; no authoritative ECS render state. Avoid overwriting concurrent changes, update `docs/spectrafx.md` and commit a new stage handoff after implementation.
