# SpectraFX Stage 30 — chunked asynchronous Metal GPU checkpoints

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Initial HEAD observed: `9d08f7f6b719294e1c648c103e0f7252ac5b7188`
Concurrent branch movement occurred; a 409 was resolved by fetching the current version and applying only the intended edit, without force-pushing.

## Source changes and commits
- `fdeca41df596280e9f9baf24fde3fba21108841e` — `engine/gfx/Gfx.h`: optional non-pure virtual `readBufferRangeAsync(BufferHandle,size_t offset,size_t bytes,BufferReadFn)`, default unsupported.
- `13d19a2434d1c8adcd325013fbe56de5eb3e9623` — `engine/gfx/metal/GfxMetal.mm`: Metal ordered asynchronous ranged blit readback, <=64 KiB per request, bounds/encoder/queue checks; completion on device owner thread in `deliverBufferReads`.
- `97a02196ff5acdab913484ebce29d1af72775171` — `engine/fx/SpectraGpuParticles.h`: `checkpointEveryFrames` opt-in interval, `requestGpuCheckpoint` API, pending state and checkpoint request/completion/failure stats.
- `81d0abc2b6bddffe6a954e51476f7f7b38e8ba57` — `engine/fx/SpectraGpuParticles.cpp`: increment `vmFrameRevision_` after compute dispatch and schedule periodic nonblocking capture.
- `90104ce0e7027b4021d75a8f6bdfb857ce4be1c0` — `engine/fx/SpectraGpuParticles.cpp`: chunked 3-buffer asynchronous capture and assembly.
- `159f02c794f5b79074a324db27432de176d53d51` — `engine/fx/SpectraGpuParticles.cpp`: partial enqueue failure cancels publication/notification.
- `c973427593ccc94ba706933cae7c73b61e570615` — `engine/fx/SpectraGraphRuntime.h`: manual request wrapper.
- `75f94659c42fee5014e2c0a84edd317010431240` — `engine/fx/SpectraGraphRuntime.h`: graph option for cadence.
- `df7c31eb22f1dc50147e04d2311d12b7a9e78c02` — `engine/fx/SpectraGraphRuntime.cpp`: pass graph cadence through to compute emitter.
- `82bf41c46985781ba9b9ec3b43f600cb7a03ff3a` — `docs/spectrafx.md`: documented API and limits.

## Runtime behavior
```cpp
aiverse::spectra::GraphRuntimeOptions o;
o.preference=aiverse::spectra::ComputePreference::PreferGpu;
o.checkpointEveryFrames=30; // 0 default; frames, not seconds
// runtime.initGraph(device, graph, overrides, o);
// runtime.step(dt) after beginFrame, outside render pass.
// On successful step every 30 GPU submissions, at most one async
// checkpoint capture is scheduled. Or call:
bool accepted=runtime.requestGpuCheckpoint([](bool complete) {
    // complete = all three raw GPU data buffers copied successfully
});
```

Each complete checkpoint contains physical-slot-indexed raw 64-byte particle state, 32-byte instance-parameter register file, 32-byte custom-attribute register file plus CPU remaining-life mirror. At capacity=4096 that is 512 KiB, split into eight requests of up to 64 KiB. The Metal RHI enqueues ordered blit copies into the same in-frame command buffer. Later main-thread callbacks reassemble only when all chunks succeeded. A shared lifetime token prevents dereferencing a destroyed ComputeParticles instance.

Crucial freshness fix: `vmFrameRevision_` is now incremented when a GPU simulation dispatch is sent. The completed capture retains its original revision. `switchToCpu` restores it only if current revision is equal and no unsent births remain, otherwise CPU fallback remains explicitly lossy and increments `lostOnFallback`. Historical checkpoints are useful for diagnostics but are not falsely reported current.

## Limitations and actual verification
Only connector source writes and branch reads performed. **NOT RUN**: native compiler, CMake, CTest, macOS Metal device, partial-queue fault injection, loss-of-device tests, bandwidth profiling, editor rendering, CPU/GPU parity and end-to-end playback.

- This is not a production guarantee of lossless GPU recovery. In the common failure case the latest completed async snapshot may already be stale when the GPU fails. Without an exact compatible snapshot, CPU recovery reports lost particles.
- A request cannot be started when the Metal encoder is open or the device is unhealthy. The RHI has a global limit of 32 pending buffer reads; insufficient capacity causes the entire checkpoint attempt to be rejected.
- Snapshot buffers have bounded sizes, but high capture frequencies can consume considerable bandwidth/CPU memory. No adaptive scheduler or delta compression yet.
- Callback is cancelled on GPU teardown; accepted request is not guaranteed to call the user after destruction. Do not pass callbacks requiring a guaranteed completion side effect.
- Source custom events, continuous emitter identity and exact visual parity are not restored by Stage 29 CPU importer.
- No new genre dependency or authoritative ECS render data introduced.

## Next Stage 31
1. Add an explicit checkpoint freshness/status API, optionally a bounded fallback policy chosen by the game (current-frame-only vs visual best-effort) with precise stale-rollback reporting. Do not silently treat stale snapshots as exact.
2. Add structured GPU telemetry and failures; consider adaptive cadence and GPU readback bandwidth budgeting, delta copies or transfer-ring architecture.
3. Audit Stage 27–30 C++/MSL API integration and regression tests, including Metal 64 KiB range read correctness, concurrent requests, callback cancellation, frame revision, partial enqueues, loss accounting and import atomicity. User continues to request functional work before running tests; label all execution as NOT RUN until performed.
4. Preserve per-stage handoffs, API/ownership boundaries and parallel agents' changes; do not force push.
