# AIVERSE LENS Stage 18C — safe Studio RT retirement and client boundary (2026-11-19)

## Branch / commits
- Branch `feature/universal-multicamera-render-targets`; main untouched.
- Studio RT retirement: `5e3c39e112bf1c7ea044c9bd43a59da71034f0d2`.
- Generic client adapter: `90a663921fb0992780348775408aced168fe0696`.
- CPU test: `a64ed85ca1d092da76fc9af4f656b8ac9145762f`; CMake registration: `23a7b205dc99992c27791fbdec5af0445d8be358`.
- Changed `apps/editor/windows/StudioCamerasWindow.cpp`, `CMakeLists.txt`; added `engine/render/CameraClientAdapter.h`, `tests/render/camera_client_adapter.cpp`.
- Previous handoff: `docs/agent-handoffs/universal-multicamera-stage18b-live-capture-runtime-2026-11-19.md`.

## Changes
- Studio no longer immediately destroys old preview textures when project changes or preview size changes. Retired Viewports keep color/depth handles and submitting frame serial. `collectRetiredPreviews` destroys them only if `Device::completedFrameSerial()` is nonzero and covers the retirement serial. Unknown completion retains resources rather than unsafe reuse.
- Studio stops previous recorder upon project switching and installs a fresh callback status; readback completion is still owned by the RHI.
- Added `CameraClientAdapter`: application-neutral integration façade over `CameraRuntimeHost`. It requires all asset cameras to output to nonzero offscreen RT keys, refusing any Screen output so it cannot overwrite the legacy GameClient/Editor host presentation pass. `loadOffscreenAsset`, `init`, `render`, `host`, `shutdownAfterGpuIdle` provide explicitly controlled lifecycle. The host supplies per-camera scene submission and calls after ECS/simulation update between GPU frame begin/end.
- New `camera_client_adapter` CPU CTest exercises a valid texture-only JSON asset, rejection of repeated loading and rejection of a screen camera.

## Explicit outstanding work / safety
- `GameClient.cpp` is **not** automatically wired to the adapter. It owns a complex RTS scene and overlay pass, and does not currently expose a proven GPU-idle shutdown contract. Wiring an automatic camera pass without the application-level frame and cleanup boundaries could corrupt screen output or prematurely destroy in-flight GPU resources. A future native application shared host should opt-in from project manifest, load with `platform::readContentFile`, submit genre scene to each camera, and tear down after confirmed GPU idle. Do not describe this as completed game-client end-to-end integration.
- Retired preview images whose backend cannot report completion serial remain held; if device is torn down without a formal `shutdownAfterGpuIdle` cleanup for the editor window, these can leak RHI handles. Add explicit editor shutdown ownership and backend idle hook before shipping.
- Editor previews still use `Editor::renderScene` instead of the full multi-camera graph, and direct HDR/EXR export is still absent.
- `CameraRuntimeHost::loadAsset` and `CameraClientAdapter::loadOffscreenAsset` currently parse independently; optimization/one-pass transaction is future cleanup.
- Vulkan/Metal/GL async callback timing, viewport retirement, shader compile and Studio UI need hardware validation.

## Tests actually performed
- Remote GitHub source inspection and successful writes only. **NOT RUN**: CMake, compiler, CTest, Metal runtime, Vulkan/GLES build, editor-client smoke.
- One consolidated final validation should build all targets and run all `camera_*` CTests plus GPU readback/PNG pixel comparison, then correct failures and document actual results.

## Next
1. Provide a shared application-level GPU completion/shutdown hook, and ensure Studio ownership can clean up retired previews at a proven idle point.
2. Wire opt-in LENS use in native game/client host with a supplied scene draw callback and existing screen pass protected.
3. Execute consolidated compile/CTest/backend tests, fix and update handoff.
