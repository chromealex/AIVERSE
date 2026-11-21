# AIVERSE MotionWeave / Universal View — Stage 117 handoff
Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open/Draft**)
Previous stage: `docs/agent-handoffs/animation-stage116-render-targets-2026-11-19.md`

## Actual starting point and source of truth

- Continued Stage 116 branch HEAD `fa314776c0bee77d7952449cb438731ad3e67aff`.
- GitHub compare before changes: Stage 116 already contains `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`; ahead 755,
  behind 0. After functional commits, ahead 762, behind 0.
- The previous main integration is the two-parent merge
  `01538817a68ac869b1e9ca12bc48ef9d9f4d56c7`.
  No rebase/force push/main write or local-worktree modification.
- Only the animation branch is modified; other agents may update other
  branches, so re-check ancestry before the next stage.

## Implemented code (exact source commits)

1. `ea3032814ade6afcbd1ac5c21f2303d81af2bf47` —
   `engine/gfx/Gfx.h`: optional
   `gfx::Device::PipelineColorWarmStatus { Ready, Pending, Failed }`
   and `warmPipelineColorStatus(handle, format)`. Its default maps
   existing bool warmup to Ready/Pending and does NOT guess an
   unsupported backend failure. Third-party/GL/Vulkan implementations
   may keep their existing bool override unchanged.
2. `e5b423178fbbbfe0807e55dbe614b0ae50079e23` —
   `engine/gfx/metal/GfxMetal.mm`: Metal now reports:
   - Ready for an existing/fully published variant,
   - Pending while an asynchronous HDR ShaderJob/PipelineJob is running,
   - Failed for invalid handles, missing source shaders, shader
     compilation rejection and pipeline compilation rejection.
   The former bool API still means "terminal/not pending" (true even
   on terminal failure), preserving prior callers' completion polling.
   Lost base handles cannot publish a late async result.
3. `0ed4992c5a5594a300ad02477b47cede27215979` —
   `engine/render/Renderer3D.h`: expose
   `GpuWarmStats::colorVariantsFailed`.
4. `75b364b2ee84598f505f5313ad1ba44cfdfab5c4` —
   `engine/render/Renderer3DModels.cpp`: checked preflight consumes
   tri-state, sets Failed rather than accidentally Ready after an HDR
   compiler rejects the variant; pending polls remove terminal
   failures and set the renderer's error diagnostic instead of
   pretending the pipeline was successfully compiled.
5. `634e7a00f573dd401e1a9d854ae781ea32d57d7d` —
   `apps/editor/windows/StatsWindow.cpp`: report actual Ready vs
   Failed HDR completion counters, not just total completed attempts.
6. `023b30b7145f50d29989c5bb81958079035e4030` and
   `f634235e766d196566e1e51275e3dbf182e3cfd8` —
   `Renderer3D.h/.cpp` model cache: cache terminally rejected
   `gfx::PipelineHandle` in `failedColorWarms_` and invalidate it
   with its shader/pipeline cache generation. A candidate that
   references the rejected variant stays Failed and does not
   repeatedly poll/recount or upload an equivalent scratch Model.

## Architecture and compatibility

- `MotionWeave` remains a **genre-independent presentation ECS**
  with a separate World, one graph clock and no authoritative
  gameplay/physics writes. GPU shader compilation and RHI resource
  management are device-owner concerns, shared by FPS/RTS/editor
  View consumers; not animation simulation features.
- Existing `warmPipelineColor` bool behavior is preserved.
  Implementations providing only the bool API report Ready/Pending
  through the new default; only a backend with explicit error state
  may report Failed. No timeout, frame counter or speculative
  "elapsed time means ready" inference exists.
- `Renderer3D::warmModelPipelinesChecked` remains the single
  transactional candidate preflight; a Failed/ Pending appearance
  cannot replace the View's last-good model/material/graph state.
- Every old renderer GPU program/RTT owner and fence contract
  from Stages 115-116 remains; Stage 117 did not change
  NEREID water selection, Prisma effects or SpectraFX.

## Files changed

`engine/gfx/Gfx.h`,
`engine/gfx/metal/GfxMetal.mm`,
`engine/render/Renderer3D.h`,
`engine/render/Renderer3DModels.cpp`,
`apps/editor/windows/StatsWindow.cpp`,
plus the Stage 117 handoff and the corresponding architecture/docs/
`AGENTS.md` updates.

## Verification actually performed

- Read back relevant source from the connected GitHub repository,
  checked existing shader/renderer warm paths and Metal pipeline
  states, updated files using the current blob SHA and branch-specific
  commits, and compared branch against main (behind 0).
- **NOT RUN:** local clone/CMake configure/native compiler/linker,
  CTest, CI, Metal/Vulkan/GL real GPU warmup, device loss,
  sanitizer, FPS/RTS/Editor visual parity, GPU regression/benchmark.
  No statement of compilation success or production stability is
  justified. Tests remain deferred by the user's features-first plan.

## Limitations / risks

1. The new status is explicit on Metal only; the compatibility default
   for other gfx backends cannot distinguish terminal failure from
   perpetual Pending until those backends expose a concrete failure
   signal. It intentionally does not guess.
2. Native Metal async jobs and last-good GPU handles still require
   real-thread/GPU lifecycle verification; a failed asynchronous
   variant must not be treated as a successfully warmed pipeline.
3. Counters reflect status observations/terminal handle entries in
   the renderer instance; they are not GPU compile timings.
4. The existing full-device-idle shutdown barrier protects live
   trace-dummy/shader/buffer resources; failed partial renderer
   initialization, standalone plugin RTT owners and device-loss
   recreation are not yet fully addressed.
5. All C++ changes were source-only and may need compiler or
   backend fixes during the validation phase. PR #22 remains Draft.

## Exact next Stage 118

- Audit and fix partial `Renderer3D::init` failure cleanup,
  `LightSystem::init` and `initTrace` rollback, and safe ownership
  of non-fenced standalone RenderTargets while retaining last-good
  resident scene state. No unsafe direct destruction of in-flight
  resource handles and no cross-genre gameplay dependency.
- Continue independent adapters (models/LODs/attachments and graph
  continuity) and other unfinished feature extraction to generic View.
- Record deferred test cases: true Metal HDR Pending->Ready,
  Pending->Failed and invalid base handle, backend compatibility,
  editor candidate rejection while MotionWeave clocks continue,
  resource-init allocation failure, real shadow/SSR/NEREID resizing.
- **Finish features first**; then build/test all native targets,
  run CTest/ASan/TSan and Metal/Vulkan/GL visual/performance parity,
  fix warnings and regressions, and only then consider PR readiness.
- Append a standalone Stage 118 handoff with actual test status
  and update `AGENTS.md`.
