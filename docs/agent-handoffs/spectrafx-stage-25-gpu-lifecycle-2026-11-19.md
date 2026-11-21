# SpectraFX Stage 25 — physical slot lifecycle, birth gating and differential snapshot uploads

Date: 2026-11-19. Branch: `feature/universal-entity-inspector-fps`. Starting branch HEAD observed: `f9bcf9f16d1270c3332950d6301495545a8c21a3`.

## Source commits and touched files
- `1705d07166ef69cea688b38c5050a4ace9b98d72` — `engine/fx/SpectraGpuParticleSlots.h`: renderer-only physical slot allocator. Generation-safe `ParticleId`, source snapshot at acquire, pending birth, release, free-list, bounded physical prefix.
- `a479fe57f9bd1733550a16b7eecb0dce85ac1ae4` — `engine/fx/SpectraGpuVmDispatch.h`: compare source snapshots against this pass's cached copy and upload changed contiguous ranges; expose optional `readErrors` diagnostic API.
- `5a349fddecd46258baeb7e8ba39297576dc47018` — `engine/fx/SpectraGpuSourceParameters.h`: disallow overwriting an active particle snapshot, mark newly born particle with `reserved=1`, expose `acknowledgeSpawn` to clear birth marker after submitted GPU spawn dispatch.
- `ecbb65b2de600d6ce45660aae8da964b9a094dab` — `engine/fx/SpectraGpuParticleSlots.h`: acknowledge mirrored particle birth when its GPU spawn has been submitted.
- `d4707176fc270093e83ce319a567fca77683173b` — `engine/fx/SpectraGpuProgramShaders.h`: Metal VM stage Spawn executes only when per-particle birth marker is set; Update continues for active particle slots.
- `e4863e9931bc05f95f1cabc9d61cec12e69f1a55` — `engine/fx/SpectraFX.h`: exports new generic native-host slot lifecycle API.
- `c365b2d7a717fa26f3b7e5babe99523cd797e733` — `docs/spectrafx.md`: public API and safety boundary.

## Runtime contract for an opt-in native host
```cpp
using namespace aiverse::spectra::gpu_program;
ParticleSlots slots{256, 4096};
auto source=slots.parameters().create({1,0,0,0,0,0,0,0});
auto particle=slots.acquire(source); // also snapshots current source values
// Upload seed into the SAME physical slot in the GPU state buffer.
// Submit MetalVmPass::dispatchSources(states,slots.physicalPrefix(),
//                                    program::Stage::Spawn,dt,slots.parameters())
// after appropriate RHI storage ordering / birth seed upload.
slots.markSpawnSubmitted(particle); // only after accepted submission
// Subsequent frames: dispatchSources(... Stage::Update ...)
// Wait for GPU use of physical slot to complete before:
slots.release(particle);
```
`ParticleSlots` is NOT an automatic GPU command scheduler. Its `markSpawnSubmitted` only records host submission, not completed GPU execution. A host must enforce fences or a completed frame slot before recycling. Sources can be retired while live particle snapshots continue until retirement/kill. Multiple independent sources use generation-safe IDs and snapshot values; no authoritative ECS state is modified.

## Performance and diagnostics
`MetalVmPass` now compares each active physical slot against its own uploaded snapshot cache, coalesces adjacent changed slots and uploads only changed ranges. The Metal RHI's storage `updateBuffer` enqueues blit encoders, preserving submission order before subsequent compute encoders. This cuts CPU upload work when values do not change but is NOT a GPU profiling result. `readErrors(count)` exposes an optional diagnostic readback through `Device::readBuffer`; a readback failure means unknown count, not zero. The GPU counter is still reset for each dispatch and cannot represent aggregate frame errors without host polling.

## Verification actually performed
Only GitHub source writes were confirmed. **NOT RUN:** native CMake/Clang compilation, Metal shader compilation, CTest, RHI execution, async fence validation, frame-to-frame snapshots, GPU/CPU semantic parity, integrated editor preview and performance benchmarks. Do not claim production readiness. The user has deferred test execution until functional work finishes.

## Explicit limitations and next Stage 26
1. **Still no production `GraphRuntime`/GPU VM auto-selection**. Existing GPU eligibility must continue refusing advanced VM effects, with unchanged full CPU fallback.
2. No integrated call site ties actual `ComputeParticles` seed uploads/physical slot lifetime and new VM pass together. Implement a single coordinated opt-in presenter/compute scheduling path with explicit ordering: seed upload → VM Spawn only for pending slots → integration → VM Update → compact/draw. Current classes provide primitives, not that orchestration.
3. Current RHI readBuffer diagnostic may be unsupported or stall; no aggregate asynchronous GPU error tracking. Add completed-frame readback or error-state telemetry with callback-safe ownership.
4. Unrecoverable GPU device transitions still drop GPU-only active particles in the legacy compute wrapper; either mirror enough CPU state to recover or signal loss loudly, respecting `RequireGpu`.
5. Verify CPU VM vs Metal error semantics (CPU skips only the failed assignment; current Metal interpreter aborts that particle's stage transaction), expression instruction/floating-point parity and per-instance changes. Unsupported programs must never silently become GPU-eligible.
6. Add stand-alone unit sources for source slot allocation, stale IDs, birth gating, delta upload, multi-source values, finite rejection, CMake target and Metal smoke. *Do not report tests passing without running them.*
7. Preserve each bounded code stage and dedicated agent handoff, avoid force-push or interference with concurrent branch/main work.
