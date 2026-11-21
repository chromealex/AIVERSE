# AIVERSE MotionWeave / Universal View — Stage 115 handoff
Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open, Draft)
Previous stage: `docs/agent-handoffs/animation-universal.md` Stage 114,
`f52ef210789cf5287ce9fb5cf6cf0bef73734a9e`

## Results / ancestry

- Main was `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, the animation branch was behind by 409 Git commits. Compared BOTH histories to their base `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
- Main had 114 changed paths, of which 108 were disjoint and preserved directly. Six shared files received a three-way content merge: `AGENTS.md`, `CMakeLists.txt`, `engine/render/PostFx.cpp/.h`, `engine/render/Renderer3D.cpp/.h`. NEREID/underwater and older MotionWeave GPU lease authoring both remain. The merged tree was inspected: exactly 114 blob differences from the pre-merge animation tree, as expected.
- Created a two-parent Git merge commit `01538817a68ac869b1e9ca12bc48ef9d9f4d56c7` and advanced only the feature ref by a non-forced fast forward guarded with expected previous SHA. Main was NOT modified.
- At the last post-feature GitHub check (before writing this handoff): branch HEAD `0a6f0f49ab3fd02c110229675be8599dc0ae7176`, current main `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, ahead 727 / behind 0. PR #22 still Open/Draft. These counts may change as independent agents commit; refresh before acting.

## Architecture and code changes

### 1. GPU shader/pipeline cache invalidation

New `engine/render/GpuProgramRetirement.h` (`daa55cfc8627b4e30699b10e14667dba6e2107ff`) defines `render::GpuProgramBatch` with pipeline and shader handles, and a persistent `GpuProgramRetirement` owner under the host's existing `RenderDeviceLifetime::Lease`. It uses `AsyncResourceRetirementQueue`, with each old program generation assigned its own GPU completion token. A Failed or Pending fence is not a release authorization. Unsupported tokens require a successful real `waitForResourceIdle`; if no safe fallback is available, the code retains/orphans resources rather than freeing GPU-visible memory. Pipelines are destroyed before shaders.

`engine/render/Renderer3D.h` (`b443ea7b0278c48c9396b4aa2bd5567026424bd8`) exposes `pendingProgramRetirements`, `collectRetiredPrograms`, `programRetirementOrphanBatches`, and a private `retireModelPrograms`. `Renderer3D.cpp` (`93692d919fa4351e269e58399a00cbad9cf8743e`) initializes the owner using the SAME host device epoch and requires draining outstanding batches before normal renderer destruction. `Renderer3DModels.cpp` (`df8c4fd3390e5e2e48835e1ccc921cc8bdf9aa23`) now detaches obsolete model shader/pipeline cache entries on `setMaterialQuality` / `setInstanceLimit` and queues them for GPU-fenced retirement instead of destroying handles immediately. Pending asynchronous compiler job cancellation remains the Gfx Device job contract.

Actual per-frame collection is wired in `apps/client/GameClient.cpp` (`92a4cab663a4e59bf62ff62854580aef2342916d`), `apps/editor/Editor.cpp` (`bac01077658a6a464746e4f82a740e7ebcd4c043`) and `apps/fps/client.cpp` (`2e658aa76758be7b2b1e9774850fdca5f24701e8`). Hosts must poll only at a real device-owner frame boundary, outside a render pass; the retirement queue is NOT safe to collect per draw.

### 2. Async GPU shader completion without repeated scratch loads

`Renderer3D::hasPendingModelPipelineJobs` and `pollPendingModelPipelineJobs` (`e4b2dd0b532dd6faed9000c72d5ad4d1c860b6c9`, `5e61364fb6b8f112bc1b1849b1e76b64272bec2c`) can advance native shader and pipeline jobs already staged inside Renderer3D WITHOUT creating another Model, material or GPU texture. They preserve cache records and classify failed completed shader jobs for the normal `PipelineWarmStatus::Failed` path.

The editor visual-source watcher (`ccf407c74e599cc36cecaa1a6cbf0269847a79dc`) holds the edit pending and skips duplicate candidate re-import while those jobs remain in progress. Editor/GameClient/FPS also poll native compiler jobs at the frame boundary (`ccf407c...`, `56cbe5f6092953a94cfb648e26399065531dd83f`, `03a548e7feb0d9f808abc5948cc05a432762503f`). A `warmPipelineColor` variant with no exposed ShaderJob/PipelineJob may still require repeated candidate staging; that is an explicit Stage 116 opportunity. The compiler queue is a renderer GPU cache, NOT a second animation clock or a gameplay-system state owner.

### 3. GPU texture resizing in PostFx, including integrated NEREID underwater

When the host supplies a `RenderDeviceLifetime::Lease`, PostFx now maintains an `OffscreenTargetRetirement` queue (`engine/render/PostFx.h` `0559652673425b8ba08568eb70028e89bd528c26`, `PostFx.cpp` `0449351712f05d6ccd4b57240326951f113fa121`). Replacing old color/depth targets, removing LRU sizes, and replacing LUT/graph images enqueue old handles instead of destroying them while previous GPU commands may still be using them. Editor and GameClient collect them at frame boundaries (`9638c6e7cbc364ac2cc38c24e561440f79fa1351`, `76303668e6a7a828d1bd9f40199a5b4f70f909cb`). On shutdown, queued handles must be drained against a proven GPU-idle barrier before the shader/pipeline resources are released; an unleased legacy standalone tool retains the old direct release policy for compatibility. NEREID underwater and depth refraction modifications from the merge remain intact. These two resource pools (PostFx targets and Editor Scene/Game window targets) share the host epoch, but not a GPU resource queue.

`apps/editor/windows/StatsWindow.cpp` `3aaff1c1dd1b276bedda893e225f49f60f24e981` displays pending Renderer3D programs and PostFx retirement handles in the existing editor GPU diagnostics.

### 4. Native Model GPU upload false success

`engine/render/Model.cpp` `03960210d4459003ff9b9b5c92a7986efeabbffb` now checks whether indexed vertex/index allocations actually succeeded, or a geometry-pool page was created, BEFORE `uploadModel` clears the CPU source buffers and returns. This prevents a false successful speculative import with absent GPU geometry. The existing `ModelStagingTransaction` retains ownership on failure and routes partially uploaded buffers/images to the GPU-fenced Model cache retirement path.

## Deferred test additions (DO NOT report as run)

- New `tests/render/gpu_program_retirement.cpp` (`28f1d72e5c8b9f379ff62229851cec32fbf86e72`) exercises Pending vs Failed tokens, idle fallback, ordered pipeline-then-shader destruction, no-token backend fallback and stale GPU epoch.
- CMake registration `render_gpu_program_retirement` in `CMakeLists.txt` `3e52717c76400a313cfd90761b68e5f95ab14c4d` under `AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN`, linked to `Threads::Threads`.
- No native CMake configure/build/compiler/link, CTest, tests/run-tests.sh, sanitizer, actual Metal/Vulkan/OpenGL GPU fence/device-loss reproduction, Editor FPS/RTS visual parity, or integration CI was performed. Only connected GitHub file/ref/tree reads, source edits, merge-tree inspection and GitHub compare/PR status checks were performed. THIS BRANCH IS UNVERIFIED FOR C++ COMPILATION OR RUNTIME AND MUST REMAIN DRAFT.

## Known limitations / risks

1. Branch now includes water/NEREID main changes, but six manual shared-file edits have not been compiler/GPU-tested together. Future main commits need another compare before claiming synchronization.
2. Renderer3D's live shader/pipeline resources still use the original teardown policy; the new GPU queue protects only obsolete model program permutations on runtime quality/instance-limit invalidation. The host must stop submissions and drain old program generations before GPU device teardown. Shadow maps, reflection RTTs, active FX render targets, terrain geometry and other RHI handles have separate policies.
3. PostFx target retirement is enabled on hosts with a device epoch lease; some standalone legacy tools remain on the old immediate-release contract. A failed backend idle barrier will now reject leased PostFx shutdown rather than destroying unsafe GPU resources.
4. Async compiler optimization avoids rebuilding candidate Models while native ShaderJob/PipelineJob callbacks are Pending. It does NOT avoid re-staging when only backend `warmPipelineColor` work remains Pending; it also does not cache entire candidate authoring snapshots across new file changes.
5. `ModelTextureCache::PreparedGpuPromotion`, native Model image staging, and offscreen RenderTargets are source-only and still require error/OOM/fence validation. An exceptional allocation failure can leave orphaned GPU handles intentionally rather than use-after-free.
6. Generic `Renderer3D`, PostFx and Model caches continue to depend on a single owner-thread gfx::Device/RenderDeviceLifetime epoch. No device-loss recreation or GPU program migration is implemented.

## Next exact Stage 116

- Inspect source changes made by concurrent teams since `01538817` and synchronize main only after a Git graph/overlap audit; never overwrite editor/VFX/water changes or force push.
- Complete the reusable Renderer3D program owner: safely retire old shadow map/trace/render-target allocations, and audit pipelines built asynchronously/failed shader programs for cleanup when hot reload is rejected.
- Extend the non-rebuilding GPU preflight to handle `warmPipelineColor` Pending without repeatedly importing scratch Models; document source revision invalidation and retry/backpressure.
- Add deferred tests for shader Failed/Pending transition, in-flight material quality switches, PostFx HDR resize + NEREID depth reflection while a GPU fence is pending, and a partial Model vb/ib creation failure.
- Finish remaining features first, then run the complete native CMake/CTest/graphics and sanitizer suites, correct all compile/runtime failures and review PR #22 before merging. Append a separate Stage 116 handoff and update `AGENTS.md`.
