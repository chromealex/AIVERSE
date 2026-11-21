# SpectraFX Stage 31 — checkpoint freshness visibility and explicit fallback loss reason

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`

## Implementation and commits

- `cccc3d52febacf13581dd2fd9bd8ef90e956ebf6` — `engine/fx/SpectraGpuParticles.h`: added read-only `GpuCheckpointStatus` with GPU-active/pending/complete/fresh/restore-eligible indicators, submitted/captured revisions, frameSerial, framesBehind and unsent-birth state.
- `3d23b4f8583978f797473dcae680c452cf47a5f3` — `engine/fx/SpectraGpuParticles.cpp`: implements status from internal bookkeeping (no RHI readback); `switchToCpu` consults precisely the same restore-eligibility predicate, and its `reason()` explicitly names stale checkpoints and lost particles when appropriate.
- `fecd1f25f3c19b0a7b8287853521e833dee1e3f2` — `engine/fx/SpectraGraphRuntime.h`: forwards status for host/editor inspection; shared CPU paths return an empty, not-eligible status.

## Host API

```cpp
const auto status = runtime.checkpointStatus();
if (status.gpuActive && status.complete) {
    // status.framesBehind: submitted GPU simulation revisions since capture
    // status.fresh: capturedRevision == currentRevision
    // status.restoreEligible: conservative same-frame, complete physical-slot
    // layout and no pending births; not a promise of lossless visual parity
}
```

`pending` denotes an in-flight asynchronous checkpoint. `complete` means that the most recently *retained* checkpoint was assembled in full, not that the latest pending request succeeded. `framesBehind` is a count of GPU simulation submissions, **not** elapsed render frames. `restoreEligible` is true only while GPU is active and the last complete snapshot matches the current submitted GPU revision, no unsent births exist, and snapshot slot-vector sizes equal the physical GPU slots. A successful CPU importer can still be refused by quality/budget/particle validation. No silently enabled stale rollback; all fallback remains current-revision-only. Saved snapshots and their serial are presentation diagnostics, not authoritative ECS state.

## Important limitations

- Recovery still cannot guarantee fidelity when checkpoint callbacks finish after a subsequent GPU simulation dispatch. This stage **does not** add best-effort stale replay or opt into lossy resurrection.
- Source/effect identity, emitter timing and RNG continuity remain outside the CPU importer. Numerical parity and material parity have not been established.
- Async readbacks need a healthy Metal device and callback delivery. Unsent births and partial failures invalidate eligibility.
- `GpuCheckpointStatus` is not a device fence or a GPU-side truth oracle; it reports established CPU-side submission bookkeeping.
- Existing CPU backends, quality policies, FPS/RTS/shared ECS are unchanged. Prisma owns postprocess; SpectraFX owns particle VFX.

## Actual verification

GitHub source reads and three successful source commits only. **NOT RUN**: native C++ build, header compile, CTest, Metal device execution, editor, GPU/CPU parity, device-loss or partial-read fault injection. No performance claims.

## Next stage 32

1. Add a bounded per-device/checkpoint bandwidth scheduler that prevents concurrent emitters from requesting more than a configured readback budget in one frame. Preserve the RHI 32-read pending cap; do not assume scheduling promises completion.
2. Export aggregate checkpoint freshness and actual transferred bytes to `GpuPoolWorld` diagnostics; allow callers to distinguish pending, stale and missing snapshots across GPU groups.
3. Audit callback completion order, releaseGPU invalidation and asynchronous partial enqueue failure. Add targeted tests after functionality is complete per user instruction.
4. Keep one stage handoff per implementation step with exact commit hashes, files, tests actually executed, branch and concrete next step; avoid overwriting other agents' concurrent changes.
