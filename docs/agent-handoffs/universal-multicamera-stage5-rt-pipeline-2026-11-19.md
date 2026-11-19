# AIVERSE LENS Stage 5 — managed RT execution for ECS cameras (2026-11-19)

## Branch, implementation commits
- Branch: `feature/universal-multicamera-render-targets` (created from `main` `58e29ab052458fffcdebebffe27022b53adb9367`).
- Code commits: `3ac9f69e3ede8419c9680167c6740c568da991c6` and `42d20bde0434dfb9967f70313efbd40f5af2eb15`.
- Affected files: `engine/render/{RenderTargetPool,CameraRenderPipeline,CameraRenderScheduler}.h`, `tests/render/camera_rt_pool.cpp`, `CMakeLists.txt`, `docs/camera-system.md`.
- This handoff must be committed separately from implementation; no changes to `main` and no force-push.

## Implemented behavior and public API
- `CameraRenderTargetPool` manages offscreen color RGBA8 or RGBA16F (only if `device.supports(RGBA16F)`), always allocates a dedicated depth attachment, and indexes images by stable ECS `CameraOutputComponent::targetAssetKey`. `depth=false` suppresses public depth access but preserves depth testing.
- `init(device)`, `acquire(key,w,h,hdr,exposeDepth)`, `find(key)`, `color(key)`, `depth(key)`, `markReady(key)`, `markInvalid(key)`, `release(key)`, `collectCompleted()`, `shutdownAfterGpuIdle()`, `activeTargets()`, `retiredTargets()`.
- Changing resolution or format lazily replaces both color/depth. Previously GPU-used targets are retired only once `completedFrameSerial()>=frameSerial()`. Unknown serials are retained until explicit *GPU-idle* cleanup, not guessed safe by a fixed frame count. On failed allocation the previously published target is preserved; a never-submitted failed partial allocation is freed immediately. Max dimension is 8192 per side.
- `CameraRenderPipeline(gfx::Device&,Renderer3D&,CameraRenderScheduler&)` executes the scheduler's ECS `CameraRenderPlan` against existing `Renderer3D::beginScene / draw / render(gfx::PassDesc, post) / resolvePostFx / gfx::Device::endPass`. Screen uses the swapchain; texture cameras use managed RTs. The host owns `Device::beginFrame/endFrame`, renderer initialization, World, assets and scene-submission callbacks.
- `execute(world,frame,seconds,submit,renderOrigin)` and `executeWithPost(world,frame,seconds,submit,postFor,renderOrigin)` return `CameraExecutionResult` (plan, diagnostics, submissions/skips). Submit signature: `submit(const EcsCameraView&,Renderer3D&)`.
- Dependencies must be declared through `CameraDependenciesComponent` (producer RT asset keys). Consumers skip if the producer RT is missing/uninitialized or failed this frame. Cyclic producer graphs are blocked by scheduler.
- Once a camera's render pass is encoded, the target becomes sampleable for dependent cameras later in the same GPU frame; scheduler `acknowledge` is invoked only on submitted/healthy device path, not failed allocation.
- `Renderer3D::ScopedCpuMdiPolicyOverride` isolates per-camera adaptive geometry feedback by `Entity::packed()`. Callers may supply one persistent PostProcess per camera; sharing a live post pointer within a single frame is diagnosed and bypassed for the duplicate view.
- Stage 5 also corrected Stage 4 scheduler ordering: texture cameras are preferred over the full-screen camera even when the latter has a higher priority; dependencies still determine texture order. Until camera stack/split-screen is built, only one full-screen camera is selected.
- Generic engine headers contain no RTS/FPS/genre dependencies. This is a **native C++ opt-in path**, not yet automatically wired into editor or game-client loop.

## Compatibility / ownership / limits
- No persistence schema registrations or save-format changes. Camera authoring components require explicit `PersistentEcs` schema/codec before on-disk use.
- No RHI API changes. `gfx::Device`, `Renderer3D` and `PostProcess` instances remain host-owned.
- No automatic visibility mask enforcement in Renderer3D: the native scene-submission callback must honor `EcsCameraView::visibilityMask`.
- No compositor, picture-in-picture, split-screen, cubemap, MSAA resolve or physical lens implemented in this stage.
- Camera-produced RT reads inside a pass **must be declared** as dependencies. The API cannot automatically inspect material graphs for self-sampling. Previous-frame feedback is not supported.
- Per-camera temporal post histories are host-owned; the pipeline rejects reuse within a frame but cannot guarantee correct isolation if the host recycles the same PostProcess across camera identities on alternating frames. A per-camera post-state registry is a next step.
- `shutdownAfterGpuIdle` must be called after GPU completion and before device teardown; this is not a portable waitIdle operation. Unknown completion retains retired textures to avoid unsafe destruction.
- `CameraRenderPipeline` is bound to one presentation World / device / renderer; reset after world replacement. Camera outputs are named asset keys, not GPU handles stored in ECS.
- No guarantee of GPU completion at return from `execute`; success means pass encoding returned and `device.gpuHealthy()` was true at that point.

## Tests actually performed
- Inspected current branch source and Gfx/Renderer3D APIs remotely, authored CTest `camera_rt_pool` with `FakeDevice` to exercise target reuse, format rejection, resize, retirement, depth exposure, failed depth allocation and cleanup.
- Stage 4 authored `camera_scheduler`; Stage 3 authored `ecs_camera`.
- Remote GitHub commits completed; source paths and branch HEAD can be checked through the connector.
- **NOT RUN:** C++ compilation, CTest, render pixel parity, Metal/GL/Vulkan GPU tests, editor/native game play, XR or performance tests. The available container cannot resolve github.com; remote GitHub connector can read/write source but cannot launch macOS builds. No claimed passing tests.

## Reproduction / next agent actions
```bash
git fetch origin
git switch feature/universal-multicamera-render-targets
git pull --ff-only
cmake -S . -B build/lens -DAIVERSE_BUILD_TESTS=ON
cmake --build build/lens --target aiverse_ecs_camera_tests aiverse_camera_scheduler_tests aiverse_camera_rt_pool_tests -j4
ctest --test-dir build/lens -R '^(ecs_camera|camera_scheduler|camera_rt_pool)$' --output-on-failure
# Next: build editor + native client and run 2-camera Metal/GL/Vulkan visual capture.
```
1. Run the above CPU tests and fix compile/link errors. Validate the scheduler's screen-last test and fake Device overrides first.
2. Add a concrete host integration that creates 2 ECS cameras (one screen, one texture), draws distinct world content from each into RT + main frame, and displays the RT on a material/UI. Check GPU pixels and UV orientation on Metal, GL and Vulkan.
3. Implement a per-Entity PostFX/temporal history store keyed by World identity, generation and RT size; reset on cuts/resize/rollback. Add independent-camera TAA/SSR regression.
4. Editor camera entity creation/inspector, camera stack, director, rigs, physical camera, portals, cubemap and XR remain P1/P2 work, not complete.
5. Commit implementation, **then** handoff separately on this branch. Respect `AGENTS.md`, `docs/metal-batching-agent-handoff.md`, `docs/editor-unification-handoff.md`.

## Remote verification checkpoint — 2026-11-19
- Verified working branch HEAD before this checkpoint: `45c6a99d7390df4ae86a28afbf51ccdf4a28d438` and this handoff was present in GitHub.
- Current `main` at the check: `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`; `main` has advanced since this feature branch forked. No merge/rebase has been attempted, so do not mistake an old base for up-to-date integration.
- GitHub Actions query for this feature branch reported **0 workflow runs**. There is no CI PASS claim or hardware test result.
- Local container attempted `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` and DNS lookup for `github.com` failed, preventing local source checkout and actual CMake/CTest.
