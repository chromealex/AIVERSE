# AIVERSE MotionWeave / Universal View — Stage 118 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open/Draft)
Previous handoff: [Stage 117](animation-stage117-hdr-warm-status-2026-11-19.md)

## Branch ancestry and scope

Continued Stage 117 HEAD `1a234b35be2b6aa773329279441c1d61ea87701a`.
The latest GitHub compare after source edits shows main
`d71c6d47e532b25fb816a8bb1f96c41e5275a165`,
ahead 779 / behind 0. The prior main merge remains incorporated.
No force push, no rebase and no mutation of `main`. GitHub edits
were made directly to the existing feature branch, using current
blob SHAs. Refresh refs because other agents are working in parallel.

This stage focuses on generic Renderer3D/LightSystem/trace GPU initialization,
*not* new animation clocks or genre-specific gameplay. Source-owner policy:
one renderer Device epoch, one shared fenced RTT retirement owner borrowed
by LightSystem; standalone LightSystem may require an explicit idle barrier.

## Implemented source changes and exact commits

1. `259b5e9e2c7b5be534c9777cd19f8e9276d187d7` —
   `engine/render/Renderer3D.cpp`: reject reinitialization over an
   active device/owner; reject replacing a non-drained target owner.
   Resource initialization is now transactional: if shader, buffer, model,
   LightSystem, trace or required renderer target allocation fails, or
   initialization throws, the renderer invokes `shutdown` after a
   *real* `Device::waitForResourceIdle` barrier. An unsuccessful barrier
   is a thrown error, **not** authorization to destroy GPU-visible memory.
   Validate main/alpha/shadow/lines/triangles pipelines and buffers,
   model fallback textures and shadow target when needed.
2. `39d9d9e92f87d3fbde0255576df824bea5e08ff8` —
   `engine/render/Renderer3DModels.cpp`: only destroy valid fallback
   model textures, and clear the handles after shutdown, including when
   initialization never reached `initModels`.
3. `639e887a9aca77a238349eef9bb2eb941fe5c92f` —
   `engine/render/Lights.cpp`: `LightSystem::shutdown` now releases
   only valid shader, pipeline, buffer and texture handles, and resets
   GPU state/cursors. This works for an aborted allocation sequence.
4. `70a1744d0a8fb940f31344d3f0478ad2eab2c672` —
   `engine/render/Renderer3D.cpp`: guard and reset the renderer's
   main dynamic buffers, scene-selection resources, pipelines and shaders.
5. `d8e2a01a1596085785c71d245eacca40b6ad78e6` —
   `engine/render/Lights.cpp`: LightSystem standalone or renderer-hosted
   init is transactional, rejects duplicate init and checks *all* required
   light/halo/decal/beam resources and map/spot RTTs. On partial
   failure it requires GPU idle before partial shutdown; if
   no idle proof is available it throws instead of releasing unsafe handles.
6. `b71aad84df5851743a361e637444bf96c6c96248` —
   `engine/render/Renderer3DTrace.cpp`: stop immediately on invalid
   blit pipeline, vertex buffer, or dummy-depth texture. Renderer init
   owns partial trace rollback, and `shutdownTrace` already checks
   valid descriptors.
7. `ad0b4ea58a5846ce4f5dc80f5fce51c77ad2d736` —
   `engine/render/Lights.h`: introduce read-only counters
   `standaloneTargetOrphans` and `standaloneRetirementIdleFailures`.
8. `31009f37fe2f2ca3580bcf9d56ae8137acbf9ab7` —
   `engine/render/Lights.cpp`: a standalone, unleased LightSystem
   no longer directly destroys potentially in-flight RTT handles.
   Without a shared owner, require a true GPU idle barrier; if idle
   is unavailable, intentionally orphan and count the pair.
9. `fb3a344757a9605c7fc4c4b0261b891db3e7be78` —
   `engine/render/Lights.cpp`: on shutdown retire the dynamic map
   color/depth pair independently of possibly stale `mapSize_`.
10. `aec0315d66f025efa0e540995f3e2b99b239588b` and
    `e81459367f089b3715a663ae001ce8aa390f32af` —
    align transactional init lambda scopes in renderer and lights.

## Dependency boundaries / guarantees

- `aiverse::View` is canonical, genre-neutral presentation;
  `MotionWeave` runs on a distinct non-authoritative ECS World.
  No changes to AnimationGraph clock, network, physics or gameplay.
- `Renderer3D` owns `GpuProgramRetirement` and
  `OffscreenTargetRetirement`; `LightSystem` borrows the latter
  until its shutdown finishes. Existing NEREID reflection and
  Prisma/PostFx resource ownership are not transferred.
- After a rollback that successfully drains the GPU, handles reset
  and the same object may be initialized again. On failed GPU idle,
  the owner remains live and shutdown is *not* claimed complete;
  hosts must not discard the Device epoch as if GPU safety were proven.
- Standalone LightSystem RTT resizing must be done at a safe
  device-owner boundary; absent a shared token queue, an idle
  barrier can stall. Failure to prove idle preserves/orphans
  textures (counters), avoiding use-after-free.

## Actual validation performed

- Inspected source on the linked GitHub repository, checked current
  `main...feature` ancestry/PR state, committed code using the live
  blob SHA and read back changed initialization and cleanup paths.
- **NOT RUN:** CMake configure/build, C++ compiler/linker, CTest,
  GitHub Actions CI, Metal/Vulkan/GL GPU tests, device failure injection,
  ASan/TSan/UBSan, FPS/RTS/editor visual parity, memory/performance
  benchmarks. These code paths are **NOT VERIFIED** for compilation or
  runtime safety. Keep PR #22 **Draft**.

## Remaining risks / limitations

1. Some backends do not implement `waitForResourceIdle()`; a
   failed partial init may throw rather than return false and requires
   the host to keep the GPU owner alive until an actual safe shutdown.
2. GPU failures thrown *during resource destruction* may require a
   separate recovery path. GPU device-loss recreation is not implemented.
3. Standalone LightSystem without a supplied target owner still uses
   synchronous full idle barriers and may orphan handles on unsupported
   backends. Prefer an actual Renderer3D-owned retirement pool.
4. The renderer-owned `retireRenderTargets` legacy fallback and
   independent terrain/FX/Prisma RTT owners need separate frame-boundary
   audits; no blanket safety guarantee is claimed for other subsystems.
5. Genre-neutral model attachment, LOD and continuous graph presentation
   still need end-to-end test/visual parity; no claim all View extraction
   is complete.

## Exact next Stage 119

- Audit and harden unleased `Renderer3D::retireRenderTargets` fallback
  and remaining independent RTT owners without creating new Device
  epochs or relying on elapsed frames.
- Continue remaining universal View/model/attachments migration with
  state continuity when assets are hot-reloaded.
- Add **deferred** regression code for first/late shader/pipeline/buffer/
  texture allocation failures, repeat init, aborted LightSystem/trace
  setup, unsuccessful GPU idle and orphan diagnostics.
- Complete feature work FIRST. Then build and run all native
  CMake/CTest, GPU visual parity, sanitizers, bench and warning fixes
  before PR can leave Draft.
- Append a separate Stage 119 handoff with actual test status
  and update `AGENTS.md`.
