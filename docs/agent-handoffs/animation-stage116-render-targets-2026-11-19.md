# AIVERSE MotionWeave / Universal View — Stage 116 handoff
Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open/Draft**)
Previous handoff: `docs/agent-handoffs/animation-stage115-main-gpu-retirement-2026-11-19.md`
Previous Stage 115 HEAD: `130e613604364d9cef5fc09bb12b20f3dda292c8`

## Scope and ownership

Complete the remaining common GPU target retirement around the animation
presentation host, without moving physics/RTS gameplay into rendering and
without modifying the single MotionWeave/ECS graph clock. `Renderer3D`,
`LightSystem` and the generic `OffscreenTargetRetirement` own device-level
GPU safety; NEREID chooses which water body is reflected, and Prisma/PostFx
owns its own composition settings. Those domain boundaries must not change.

The branch already included the true Stage 115 main merge
`01538817a68ac869b1e9ca12bc48ef9d9f4d56c7`, incorporating NEREID and
SpectraFX changes without deleting animation/renderer work. At the start of
Stage 116, GitHub compare reported ahead 730/behind 0 relative to main
`d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
Refresh both refs before the next change because other agents work in
parallel. No direct main writes or force pushes were made in Stage 116.

## Implemented changes / exact commits

### Universal GPU target ownership

- `28c15c57c731ad506ed46264924fcec305dca7f9`: the
  `Renderer3D` API exposes `retireRenderTargets`,
  `pendingTargetRetirements`, `collectRetiredTargets` and an owning
  `std::unique_ptr<OffscreenTargetRetirement>`.
- `002b8c7b63b42b62cc6f023b1321f41e1e2f644d`: `LightSystem`
  borrows the renderer's existing target owner using
  `setTargetRetirement(owner)`; it never creates its own GPU epoch.
- `b98fbc4e73f4893f92f781b750d6e66ac1f31627`: replace
  light-map colour/depth, spot-shadow, static light grid and
  NEREID-compatible planar reflection colour/depth targets only after a
  valid replacement has been allocated. Old handles go to GPU retirement.
  Allocation failure preserves previous resident targets rather than
  exposing an incomplete pair.
- `8087e7535a5c7182b579b1be2c35ec1cfe93700a`: `Renderer3D`
  creates the common owner before GPU initialization, loans it to
  `LightSystem`, retires shadow depth maps and scene selection masks,
  and requires proven `Device::waitForResourceIdle` before destroying
  live GPU buffers/pipelines. At shutdown, target queues drain BEFORE
  renderer-owned Device epoch teardown. LightSystem drops its borrowed
  target pointer after normal shutdown.
- `e36dc74208972468ce02e54dab97550c0e8b3d96`:
  `Renderer3DTrace::beginSceneCapture` uses that same queue for
  size/HDR SSR capture LRU eviction, and sends failed partial
  color/depth allocations for safe retirement.
- `8cbecfc8cf48b056996a4c0438fdcc8b05ce516b`:
  scene-selection mask resizing no longer immediately destroys
  potentially in-flight GPU textures; it keeps last-good targets on
  failed replacement.
- `39280e906294b4977ff0f74083bbb27c89c8b206`:
  `setShadowMapSize` reports failure if requested High resolution
  was not actually published.
- `b9e3199361d445ee7d21cfa87c5a59e3ec435542`,
  `c5165bee5d1f0eb4555bf5ffcbf24c63bf9ef601`,
  `b475d513549011dc943fd883dc8082a01e9341b8`: Editor,
  GameClient and FPS call `collectRetiredTargets` only at the
  presentation-owner frame boundary when pending target pairs exist.

### HDR GPU pipeline completion without scratch re-import

- `8c503fbd156734120a97a8998611b440b3d0d141`: introduce
  `Renderer3D::pendingColorWarms_`, tracking backend
  `warmPipelineColor(RGBA16F)` requests that have no ShaderJob.
- `d4850a837761361a6a72df5000c770ccb3e42d19`: existing
  `hasPendingModelPipelineJobs` and `pollPendingModelPipelineJobs`
  also track/advance these HDR variants. The same pending handle is not
  re-requested while the original compiled pipeline is waiting.
  Invalidating model shader permutations clears pending color handles
  before old pipeline GPU retirement.
- `e36c09a7786cc1289171760ca5b560dc06221d50` and
  `b07231a4d4186d878d505d9f00adcce43d5a986c`: expose
  `GpuWarmStats` (pending ShaderJob/PipelineJob/HDR variants,
  color poll attempts and completed variants).
- `502d7a662c0dff90ac519b94b681ace01af946da` and
  `43f5a90b1b653faeab35a1787624d96770f8c4e9`:
  expose owner target queue failures/orphaned pairs.
  `9e7aa544b4bdb656feffc3622cea4b8cb408c0f1`:
  Editor Stats shows both the target retirement and compiler diagnostics.
  No second AnimationGraph evaluator or gameplay writer has been added.

### Deferred regression code (registered, NOT RUN)

- `1f471ebeddbc55cbf0160aa35a5b601d986a36b7` extends existing
  `tests/render/model_image_transaction.cpp` with a shadow-only
  target, SSR colour/depth, planar reflection colour/depth, failed
  partial target and delayed/failed GPU fence behavior.
- `d3aa40f5bca704c368ecbbce9fe21ae2a30d24d8` extends the same
  registered native fixture with failure to allocate indexed GPU
  vertex/index buffers without losing source CPU geometry.
- No tests executed. Existing `render_model_image_transaction` CTest
  registration is reused; no second redundant executable is added.

### Documentation

Stage 116 sections appended to `docs/view.md`,
`docs/models.md`, `docs/animation-graphs.md` and `AGENTS.md`
(commits `4d14a3d6d0dc7bfa28e2c16da4f9cdbcb8e640b3`,
`3f5025e61cae28bb974a32673c6cbe38bd69ca22`,
`75c51dab86329f5d039cd561ff34e7db2be7ca96`,
`a053da7ab74510898edce4ea5dbab33d6137faaf`).
Update this handoff and the central animation handoff after subsequent work.

## Modified files

- `engine/render/Renderer3D.h`, `Renderer3D.cpp`,
  `Renderer3DTrace.cpp`, `Renderer3DModels.cpp`,
  `Lights.h`, `Lights.cpp`
- `apps/editor/Editor.cpp`, `apps/editor/windows/StatsWindow.cpp`
- `apps/client/GameClient.cpp`, `apps/fps/client.cpp`
- `tests/render/model_image_transaction.cpp`
- `docs/view.md`, `docs/models.md`,
  `docs/animation-graphs.md`, `AGENTS.md`, this handoff
- No new genre-specific ECS component is necessary for GPU resource
  retirement; native game code may use the same generic owner contract.

## Actual validation and limitations

**ACTUALLY PERFORMED:** connected GitHub branch/PR/head compare,
inspection of Renderer3D/trace/lighting/postFX/owner sources, direct
GitHub file edit commits and readback of existing APIs. **NOT PERFORMED:**
local clone/CMake configure/build/link, CTest, GitHub Actions execution,
real Metal/Vulkan/OpenGL GPU fences, device recreation, NEREID water
SSR/planar reflection comparison, FPS/RTS/editor screenshots, ASan/TSan,
performance or memory measurement. Native compile/runtime/GPU safety
remains **UNVERIFIED**; PR #22 must remain Draft.

Known risks:

1. `OffscreenTargetRetirement` only protects the GPU resources routed
   through it. Trace dummy targets and other active Renderer3D buffers
   are destroyed directly only at shutdown, now guarded by a full GPU
   idle barrier. There is no automatic device-loss resurrection.
2. `LightSystem` borrows an owner pointer; parent `Renderer3D`
   must initialize the target queue before `lights.init`, clear the
   pointer after `lights.shutdown` and drain after all owners stop.
3. Renderer-owned target retirement is not Prisma's postFX target
   ownership; both share the GPU epoch but use distinct queues.
   Other modules with custom RTTs must be audited individually.
4. `gfx::Device::warmPipelineColor` exposes **bool readiness only**.
   It is impossible to distinguish permanent backend compile failure
   from a long-running Pending status without extending the API.
   Outstanding HDR handles remain pending; editor statistics exposes
   the queue. This stage prevents repeated scratch import but does
   not claim automatic retry timeout or recovery.
5. No source and shader changes in parallel agents were overwritten;
   however future `main` commits must be compared again.
6. Simulated fence tests are source declarations only until compiled;
   no native compiler/link or real backend result exists.

## Exact next Stage 117

- Re-check `main...feature` ancestry and newest parallel merges.
- Extend the generic RHI with an optional explicit result for
  `warmPipelineColor`: Ready/Pending/Failed, preserving backward
  compatibility. Use this to finish the GPU preflight failure contract
  without indefinite polling. Add matching backend implementations
  only where supported; never infer completion from elapsed frames.
- Audit `LightSystem` and `Renderer3D` shader/RTT initialization
  failures, trace dummy target cleanup, and other per-module GPU
  render-target owners (terrain/FX/Prisma) for genuine frame-boundary
  retirement; do not move water gameplay into renderer.
- Add deferred tests for failed shadow/reflection allocation with
  unchanged active handles, HDR warmup Pending->Ready/Failed,
  actor/LOD/attachment graph state continuity, and integration with
  NEREID underwater/postFX in the Editor.
- Finish all outstanding functional work FIRST, then run complete
  native CMake build/CTest, sanitizers and real GPU visual parity.
  Fix compile failures and warnings before marking PR #22 ready.
  Append another standalone Stage 117 handoff.
