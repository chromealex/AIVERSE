# SpectraFX Stage 28 — complete Metal snapshot export and async VM diagnostics

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting branch head: `54feaa2b3899e93a03e01c6a2af5e8890443e3f4`

## Files and exact commits
- `engine/fx/SpectraGpuCheckpoint.h` — `36d0dad0d69a843669941e95da3812ea075a77bc`: new `GpuCheckpointParticle` (128 bytes), `GpuCheckpoint` with physical-slot array, remaining-lifetime mirror, frame serial, and explicit completeness.
- `engine/fx/SpectraGpuParticles.h` — `c8ea4a826d1a0155aa1a681f196f2fe4c8a332ca`, `41d26befd053642b8336f8211027453b4030bf44`: declare GPU capture and asynchronous diagnostic methods, lifetime guard, request-in-flight state, frame revision.
- `engine/fx/SpectraGpuParticles.cpp` — `c24f281f439127e8fb6adaf0f3610a9f3d5b6274`, `f697715a4ff0df6fcd904448976416700b3f5fa1`: capture all particle state / params / custom attribute buffers between completed frames; detect missing buffers, pending births, failed reads; use Metal RHI async status readback with callback lifetime and frame-revision checks; clear lifetime on GPU teardown. No effect on gameplay ECS.
- `engine/fx/SpectraGraphRuntime.h` — `89f9297b69a967e0894faf4a7ec662795d98dda5`: public opt-in graph host wrappers.
- `docs/spectrafx.md` — `e2e100d54d4943c9e1f7ca723f12cdeddb1264e3`: authoring/runtime safety constraints.

## Implemented APIs
```cpp
// Only between completed Metal frames; blocks and may fail.
// The checkpoint is not a savegame or a CPU-imported particle pool.
aiverse::spectra::GpuCheckpoint checkpoint;
bool captured = runtime.captureGpuCheckpoint(checkpoint);

// Request during an active Metal frame, OUTSIDE render/compute encoders.
// A callback is delivered later if resources remain alive.
bool accepted = runtime.requestGpuVmErrors(
    [](bool success, uint32_t failures) {
        if (!success) return; // unknown, NOT zero
        // telemetry only; no automatic fallback
    });
```

The public checkpoint preserves the **physical slot ordering** of all GPU particles, including dead slots (positionAge.w below zero), state float4s, program parameter float8, custom attribute float8, CPU lifespan mirror and frame serial. This avoids dropping VM runtime registers during export. Checkpoint capture requires no pending unsent births, a healthy GPU, valid buffers, and an RHI that permits synchronous reads outside frames.

Asynchronous diagnostic requests use existing `Device::readBufferAsync`, limiting to one outstanding request per instance, and avoid stale writes to a newer frame's stats. A destroyed or fallback-switched GPU runtime invalidates the token, so delayed callback bodies do not dereference a dead object. A failed request returns false and no callback; accepted requests can still be cancelled by teardown.

## What has NOT been implemented
- **Not a lossless GPU-to-CPU recovery path.** `fx::ParticleSystem` has no suitable import API for live particle positions, lifetimes, attributes, and per-instance VM values. The saved snapshot is available to the host only and is not yet applied to the CPU simulator.
- A failing/unhealthy GPU may no longer support checkpoint reads. Periodic verified pre-failure snapshots and a transactional CPU import are needed for actual recovery. Do not claim that a GPU failure can be recovered transparently.
- No rolling GPU checkpoint history, automatic capture interval, bandwidth budget, or async large-buffer checkpoint copying; this Stage 28 snapshot is **blocking** and opt-in.
- No graphics API portability beyond Metal's already existing RHI readback behavior. The GPU-only experimental VM remains opt-in; existing simple CPU fallback and `RequireGpu` policy are unchanged.

## Verification (actually executed)
Successful GitHub source writes only. **NOT RUN**: CMake/native C++ compilation, Metal shader compilation, CTest, macOS Metal RHI device run, callback lifetime tests, checkpoint/parity checks, FPS/RTS integration, injected device failure or CPU restoration.

## Next precise Stage 29
1. Design and implement `fx::ParticleSystem` transactional `importLiveParticles` with a constrained, validated particle state structure (no genre dependencies), preserving attributes/VM snapshots, lifetime, source ownership and normal render batching. Handle full budget and invalid input atomically.
2. Add last-known-good GPU checkpoint ownership with explicit update cadence, frame completion and timestamp. Only switch to CPU from a confirmed complete checkpoint; otherwise continue current explicit non-lossless fallback and report drops.
3. Extend asynchronous state-buffer readback with offset/pagination or a bounded GPU copy staging approach to avoid stalls for all 4096 slots; instrument bandwidth and callback cancellation.
4. Compile the source, execute tests and Metal device smoke when user changes the current features-first policy; never describe untested behavior as validated.
5. Commit separate code and handoff at each stage, preserving concurrent agents' work in this branch.
