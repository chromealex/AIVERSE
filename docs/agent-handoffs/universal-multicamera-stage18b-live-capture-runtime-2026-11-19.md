# AIVERSE LENS Stage 18B — Studio live RT capture and generic runtime host (2026-11-19)

## Branch and commits
- Branch: `feature/universal-multicamera-render-targets`; main untouched.
- Studio capture commit: `12194a94d03ec4ff21794595e7aa1c3881d94db5`.
- Generic runtime host commit: `cf9ed7664ea85b940caf2a1e18681ad46c19fbe3`.
- Changed `apps/editor/windows/StudioCamerasWindow.cpp`, added `engine/render/CameraRuntimeHost.h`.
- Continues `docs/agent-handoffs/universal-multicamera-stage18-studio-native-2026-11-19.md`.

## Studio behavior
- The selected ECS-authored camera renders into a real GPU `Viewport` color/depth RT using the existing editor renderer.
- New `Capture PNG` button schedules `CameraFrameRecorder::tickTexture` / `Device::captureTexture` against that rendered offscreen RT, then writes `<project>/captures/lens/camera-<stableId>.png` from the owner-thread callback.
- Displays queued/pending/failure/saved messages. Only one in-flight capture per recorder; while pending, preview RT dimensions remain unchanged to avoid invalidating the capture source during a resize.
- Capture callback owns shared status and filesystem paths rather than raw window refs; switching projects invalidates the old recorder callback via `stop()`.
- Limitations: The Studio preview uses Editor::renderScene and not the `CameraRenderPipeline::executeManaged` graph; it previews one authored camera rather than orchestrating multiple camera outputs. It currently captures SDR preview only, not direct HDR EXR or the HDR tone-map resolve path. Automatic video capture is not connected in the editor.

## Portable native runtime bridge
- New `CameraRuntimeHost` owns a camera-specific ECS World, SceneHierarchy, CameraRenderScheduler, CameraRigEvaluator, CameraPostFxPool and CameraRenderPipeline.
- `loadAsset(jsonText,error)` validates/loads packaged camera JSON without platform filesystem dependency; cannot reload while a GPU pipeline is active.
- `init(device,renderer)` binds the supplied native gfx::Device and Renderer3D.
- `render(frame,seconds,dt,submit,renderOrigin)` updates the camera SceneHierarchy and executes the managed ECS camera plan with a caller-supplied scene submission callback.
- `requestCapture`, `markDirty`, `entities`, `world`, `pipeline`, `postFx` expose runtime functionality; `shutdownAfterGpuIdle` performs controlled cleanup.
- The game/application host must call it after scene simulation updates and between gfx begin/endFrame. Its own world is independent of the actual game world; cameras referring to gameplay entities need explicit stable-ID binding or a future mode for external World ownership.
- The existing RTS-centric GameClient and FPS client entrypoints have NOT been changed or wired automatically. A future unified client application layer is required to load camera assets and submit all genre-independent scene geometry.

## Tests actually performed
- Only remote source inspection and successful GitHub file writes. NO CMake, C++, Metal/GL/Vulkan/GLES compilation, CTest or device screenshot tests have been run.
- Validate Studio PNG saving/error handling, callback after project switch, render-target resize during readback, and GameClient/Editor ABI. Probe generic CameraRuntimeHost init/shutdown order and camera_world lifetime at final compilation.

## Known risks / next steps
1. The Studio project switch still immediately destroys its previous viewport RT: async readback lifetime on backend project transition requires explicit deferred GPU retirement or a wait-free captured-resource lifecycle. Current recorder callback cancellation does not itself extend GPU texture lifetime. Address this before release.
2. Do not claim Scene camera controls are fully genre independent: the legacy Editor.cpp includes RTS camera for the original Scene view. The new LENS layer is genre independent but the legacy scene still needs migration.
3. Connect the generic host in an application-neutral client lifecycle with real scene submission. Do not attach to RTS/FPS-specific modules as a core dependency.
4. One consolidated build and test run after closing all features: CMake, CTest camera_*, editor/runtime, Metal GPU, Vulkan/GLES compile-only, PNG pixel parity.
