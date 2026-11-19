# AIVERSE LENS Stage 18D — device idle barrier and opt-in GameClient RT cameras (2026-11-19)

## Branch / code
- Branch: `feature/universal-multicamera-render-targets`; main untouched. No merge or force-push.
- RHI contract: `4420684704a2702c286025ddae037bcb55611e6a` (Gfx.h).
- Vulkan: `f32b27b72c8360991783b5436e833dc96eb843c9`, `425525ee20256bff759c1c61cc715d950f55ee95`.
- Metal: `5e7743b14b99af972359f35d466a0a07f46230e9`, `82ef58da6e2a9fc1dc3a8f4fb93945e7f1bc8e7f`.
- GL/GLES: `1d11415b1b55e4daa9b9a2f859f1c5ea77a22793`.
- Editor lifecycle: `a791b02b350d3a1a88051816822a7090bff6da49`, `db6cebef0d2b464465ebebdc778985e1fd7a6c27`, `053cd6939afd046278b6eb087120c8e2c50adad5`.
- Client integration: `c5757ef3100770c2d00ae71a671b9d67690a355b`, `7f8e0f2ea2d4c77b7c1fe2191e857226c44d30ca`, `2327c799d82366ec13b055c352b3b0ca06d12a46`.
- Earlier work: `docs/agent-handoffs/universal-multicamera-stage18c-client-safety-2026-11-19.md`.

## Implemented API
- Optional `gfx::Device::waitIdle()`: returns false on unsupported backend. Must be called from the owner thread outside active rendering. Metal queues a marker command buffer and waits; Vulkan uses vkDeviceWaitIdle and flushes completed RT readbacks; GL/GLES glFinish. **Shutdown-only**, never invoke per-frame.
- `EditorWindow::shutdownAfterGpuIdle(Editor&)` is optional. `Editor::shutdown` requests idle before RHI resource teardown, then notifies windows only when success is confirmed. StudioCamerasWindow stops recorder and releases active and retired Viewport textures through device on this proven idle point.
- GameClient optionally reads `game.json` `lens.enabled=true` during renderer init, loads packaged `cameras.json`, rejects screen outputs via CameraClientAdapter, and initializes a persistent generic CameraRuntimeHost.
- While an active match renders, after the main screen/UI pass ends, GameClient invokes LENS render with the current scene, drawing world scenery + RtsView into individual offscreen camera outputs. These are separate texture passes; the legacy `cam_` and screen output are not replaced. Diagnostics printed only with `AIVERSE_LENS_LOG`.
- GameClient::shutdown waits for GPU idle before releasing the LENS runtime and then continues ordinary renderer teardown. On failure, it leaves the RHI-managed resource teardown to device shutdown instead of falsely claiming idle.
- All generic LENS code remains independent of genres. GameClient's optional callback is intentionally genre-specific to its own RTS scene, not to the engine/render/ core.

## Usage
```json
{"lens":{"enabled":true}}
```
- Place `cameras.json` in the normal platform content root. Cameras must all use `"output":{"kind":"texture","key":981,"width":256,"height":256}` (example keys must be unique). Screen output is rejected in this adapter; the standard client owns presentation.
- A native game may use CameraRuntimeHost directly with custom scene submitter and screen targets. GameClient does not auto-expose LENS RT keys to UI/materials yet. It produces textures through `CameraRuntimeHost::pipeline()->targets()`; an explicit consumer must request/resolve them.
- Recording, native EXR and editor full multi-camera preview remain separate work.

## Limitations and risks
- **No compilation or GPU testing has happened.** The exact Metal/GL/Vulkan idle behavior and GameClient screen/offscreen pass parity require hardware validation.
- Vulkan::waitIdle is explicitly blocking. Metal queue marker command is also blocking; never call it while a command buffer is uncommitted.
- GameClient's extra `view_.draw` call can have presentation-layer side effects; verify whether it repeats telemetry/events/FX. A future per-genre pure geometry submission callback should be extracted to avoid duplication.
- Editor's `clientHost_.stop` and external platform lifecycle still need GPU completion ordering validation.
- The broader engine has no forced global shutdown call for all devices; this is an optional API and only editor/client lifecycles were touched.
- Check that Metal `compute_` and callback delivery semantics compile on Apple builds; check GL loader for `glFinish` availability on each target.
- Camera adapters only accept dedicated texture outputs. The project file parser still supports screen cameras for applications that own full presentation.

## Tests actually run
- GitHub source edits and commits succeeded; **no** compiler, CMake, CTest, Vulkan/GLES compile, GPU tests, Studio or client smoke test were executed.
- Consolidated test plan: build editor/client/runtime, `ctest --test-dir build/lens -R 'camera_|ecs_camera'`, Metal RT capture and idle callback ordering, Vulkan/GLES compilation, project load smoke with lens on/off, screenshot parity, project switch during readback, verify no UI regression. Fix all diagnostics before claiming finished.

## Next
- Audit the opt-in callback's scene side effects and extract repeatable geometry-only scene submission where possible.
- Add a named API for game/UI to consume output RenderTextures without exposing transient GPU handles in ECS.
- Complete consolidated validation and repair; then assess branch merge.
