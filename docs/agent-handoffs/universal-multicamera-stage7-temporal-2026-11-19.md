# AIVERSE LENS Stage 7 — managed camera post effects / rig integration (2026-11-19)

## Provenance
- Branch `feature/universal-multicamera-render-targets`; implementation SHA `c20ac7740a2a345323b9459953d60ce65ffcfe24`.
- Added `engine/render/CameraTemporalPolicy.h`, `engine/render/CameraPostFxPool.h`, `tests/render/camera_temporal.cpp`; changed `engine/render/CameraRenderPipeline.h`, `CMakeLists.txt`.
- Parent Stage 6 handoff: `docs/agent-handoffs/universal-multicamera-stage6-rigs-2026-11-19.md`.
- No main merge, force push, or local GPU work.

## Implemented contracts
- `CameraTemporalSignature` captures camera pose/projection, output size and RT asset key; `invalidateCameraHistory(previous,current)` resets on invalid history, size/format/target change, hard camera cut (distance/rotation), material FOV or ortho-size changes. Small motion retains history.
- `CameraPostFxPool` owns independent, stable `PostFx` + optional `ScreenLighting` per generation-safe `Entity::packed()` in one World. `configure(settings,quality,enableTemporal)`, `setImageLoader(loader)`, `bindWorld(world)`, `prepare(view,width,height)` (camera jitter), `get(entity)`, `resetHistory(entity)`, `shutdownAfterGpuIdle()`. World change invalidates presentation history. No temporary shader/texture handles enter ECS.
- `CameraRenderPipeline::executeWithPostAndModifier` applies a post-scheduling, pre-render per-view modifier in a transient copy. The existing `execute` and `executeWithPost` signatures remain callable.
- `CameraRenderPipeline::executeManaged(world,frame,seconds,dt,submit,CameraRigEvaluator&,CameraPostFxPool&,origin)` wires rig, physical FOV/shake/rail, temporal jitter/postprocess and ECS RT output into one C++ host call. Host owns renderer device/frame and GPU idle lifetime. Editor/native host adoption is not automatic.
- Added CPU-only CTest `camera_temporal`; no GPU-only extra libraries. Separate ScreenLighting instances prevent one camera's prior matrices/jitter/SSGI history leaking into another when used through this pool.
- No change to network serialization/authoritative data. CameraRig/Lens/Shake ECS components still need explicit PersistentEcs codecs before save/load, and physical DOF/ISO exposure have not been integrated into shader effects.

## Validation truthfulness
- **NOT RUN:** CMake configure, C++ compilation, CTest, Metal/GL/Vulkan rendering, render screenshots, Editor/FPS/RTS end-to-end. Deferred until one final test batch at user's request; GitHub code commit is not compilation evidence.
- Test plan: `cmake --build build/lens --target aiverse_camera_temporal_tests`, followed in final consolidated test pass by `ctest --test-dir build/lens -R '^(ecs_camera|camera_scheduler|camera_rt_pool|camera_rig|camera_temporal)$' --output-on-failure`.
- Risks: PostFx/ScreenLighting allocation and shutdown have GPU resource lifetime constraints. Must call `CameraPostFxPool::shutdownAfterGpuIdle` only after queue completion, before destroying Device. No portable Device waitIdle API. Post histories may consume memory for many camera IDs until shutdown; future budget/retention policy is required.
- Host must call `CameraRigEvaluator::reset()` when World object is reused at the same address after rollback/restart; `CameraPostFxPool::bindWorld` detects pointer change but not same-address recreation. Camera cuts should call `resetHistory` explicitly.
- Need real Metal device validation of screen-space depth sampling, temporal histories and RT sizes before declaring production-ready.

## Next step
- Camera stack / viewport and picture-in-picture compositor: explicit GPU composition from offscreen RTs, not multiple screen clears, and editor gizmos/inspector for multiple ECS camera entities. Add a separate code commit and handoff; do not run tests before feature completion. Then capture formats, cubemap/panorama and advanced physical/external integrations.
