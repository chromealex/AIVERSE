# SpectraFX Stage 3 — read-only external acceleration fields (2026-11-23)

## Source, branch, commits and tests

- Repository `chromealex/AIVERSE`; branch `feature/spectrafx-significance-data-20261123` (PR #113).
- Base for this branch was `main` `87ba5b1439267bea86c570489e8869b3f7641a0e`; concurrent main changes require review before merge.
- Code commits: `8cac11de` acceleration field registry, `ce05b410` GPU/twin field integration, `643ea23b` shared playback callback + safe pool bypass, `e8f9d70c` public scene-host binding, `38b73a81` umbrella header, `86583098` contract checks, `cc571184` header compilation smoke. Verify branch HEAD before attributing a later change to this stage.
- **Execution evidence:** No local native build or GPU run in this tool environment. Contract tests have been authored but **NOT yet verified as PASS**. A focused macOS/Linux GitHub Actions workflow was created in `.github/workflows/spectrafx-significance.yml`; inspect current run results before claiming success. The broad PR CI has failures unrelated to this change: `engine/voxel` layering (13 violations), GameCore override diagnostics and Android plugin staging missing `modules`. Those are not automatic proof this stage fails or passes.

## Changes

- `engine/fx/SpectraFxAccelerationFields.h`: standalone, capped 16-provider registry. Each host provider is an injected read-only position→acceleration callback. Stable numeric IDs permit replace/unbind. Validate finite positions and returned values; skip outside/invalid provider output; weighted sum clamped to ±1000 m/s². `evaluate` optionally emits per-call accepted/outside/invalid counters. These are **visual** forces, not physics authority.
- `engine/fx/SpectraFxHybridEmitter.h`: optional `setExternalAcceleration(float3)`, `origin()` and `externalAcceleration()`. Per-frame GPU/CPU-twin `slots_.step` reads the same locally composed `SpectraFxGpuEnvironment` with additive field acceleration, preserving authored `config_`. Stop/reset clears the runtime correction. Never change gameplay ECS or serialized asset.
- `engine/fx/SpectraFxHybridPlayback.h`: optional `setAccelerationSampler`; sample each active keyed and one-shot **dedicated** emitter once per frame before sim, then pass the same correction to Metal compute or CPU twin. Reset correction to zero when the sampler returns false or is removed. New emitters with live fields avoid homogeneous GPU pools, because one group cannot represent different per-source acceleration. Other shared-pool behavior unchanged when fields are disabled. Existing already-pooled emitters do NOT migrate automatically.
- `engine/fx/SpectraFxSceneHost.h`: host-owned callback registry and `bindAccelerationField`, `unbindAccelerationField`, `clearAccelerationFields`, `accelerationFields`. Hooks into playback without genre dependency. Register AFTER project init enables playback. Shutdown tears down playback first and clears callback registry, preserving lifetime.
- `engine/fx/SpectraFX.h`: exports field API to native modules.
- `tests/spectrafx_contract_test.cpp`: added registry weighting/replacement, NaN rejection, unbind and emitter correction/stop contracts.
- `tests/spectrafx_compile_smoke.cpp`: include and instantiate fields from a native TU.

## Example for a host bridge to PHYSIS

```cpp
// After host.init() and configureEntityEffects() enabled playback:
host.bindAccelerationField(1,1.0f,
    [&worldWindSnapshot](aiverse::render::float3 pos,
                         aiverse::render::float3& acceleration) {
        // Native host resolves PHYSIS from a stable presentation snapshot.
        // Never read mutable ECS world objects from the GPU/render thread.
        if (!worldWindSnapshot.contains(pos)) return false;
        acceleration = worldWindSnapshot.sampleAcceleration(pos);
        return true;
    });
// Before deleting the environment snapshot:
host.unbindAccelerationField(1);
```

The `worldWindSnapshot` example is an integration placeholder and is NOT a declared AIVERSE type; implement an adapter in the relevant native host. No PHYSIS-specific direct include is allowed in `engine/fx`.

## Constraints

1. **Dedicated GPU and its CPU twin only**. Legacy `cpuEffect` fallback is not adjusted dynamically; this is an explicit partial feature. Dedicated assets normally share the same CPU/GPU evolution math but exact hardware parity must be verified.
2. GPU pooled sources already running when a provider is bound keep their old homogeneous environment until stopped/restarted. Newly played sources while fields are enabled do not use shared pooling. A later stage must implement field textures or per-source acceleration for pooled arenas with parity tests before re-enabling.
3. Provider callback never runs during an open render pass; no RHI allocations/readbacks are allowed inside it. It must be bounded, thread-owner-safe, nonthrowing, and must not retain mutable ECS references.
4. No synchronous readback, renderer handles in ECS, gameplay damage/impulse modifications, or silent quality downgrade.
5. The camera significance system from Stages 1 and 2 is independent and opt-in. SpectraFX remains separate from PRISMA HDR resolve and the LENS camera pipeline.

## Acceptance and next steps

1. On clean macOS/Linux build, compile `aiverse_spectrafx_contract_tests` and `aiverse_spectrafx_header_smoke`, run `spectrafx_contract` and `spectrafx_architecture`.
2. On Apple Metal with GPU and CPU twin forced: compare trajectories after injecting an identical constant and position-dependent acceleration field; verify exact draw/readback parity and lifetime safety.
3. Load a real PHYSIS snapshot from a host adapter, sample at two emitter origins and prove different motion without genres.
4. Benchmark up to 64 dedicated emitters plus field callback costs; test registration/unregistration between frames, scene teardown and reset.
5. Stage 4: typed Graph IR/Data Interfaces and editor bindings. Keep the next stage as its own handoff; do not mark Vulkan/GLES/Fluid/Studio implemented.
