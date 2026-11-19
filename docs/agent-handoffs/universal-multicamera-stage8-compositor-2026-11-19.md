# AIVERSE LENS Stage 8 — ECS camera split screen, picture-in-picture and overlays (2026-11-19)

## Provenance
- Branch `feature/universal-multicamera-render-targets`.
- Implementation commit `62d7bd263ff310dc1d635f4284977e059d05c460`; no main mutation.
- Files: `engine/sim/CameraViewportComponents.h` (new); `engine/render/CameraCompositor.h` (new); `engine/render/EcsCameraRender.h`, `engine/render/CameraRenderScheduler.h`, `engine/render/CameraRenderPipeline.h`; `tests/render/camera_layout.cpp`; `CMakeLists.txt`.
- Stage 7 handoff: `docs/agent-handoffs/universal-multicamera-stage7-temporal-2026-11-19.md`.

## Implementation and APIs
- ECS `CameraViewportComponent` defines normalized viewport rectangle, enable flag. `CameraStackComponent` defines composition order, alpha and explicit overlay opt-in. They are fixed-point authoring data, not GPU handles or genre classes.
- `EcsCameraView` carries optional viewport/stack components. `render::cameraViewportRect` converts normalized fixed-point coordinates to validated top-left RHI pixel coordinates.
- `CameraRenderScheduler` allows multiple screen cameras when any screen view opts into viewport/stack. Without layout opt-in, the legacy single selected screen camera is preserved. Texture dependency ordering still precedes screen rendering.
- `CameraRenderPipeline` captures each composited screen camera into an isolated offscreen color+depth texture owned by its own `CameraRenderTargetPool`, keyed by generation-safe `Entity::packed()`. Normal texture-output cameras retain user-visible target asset keys in their original pool. A deferred screen camera can reuse its prior image.
- `render::CameraCompositor` composes finished screen-camera texture layers through **one clear and one screen pass**. Uses shader-based texture sampling with backend Y orientation, alpha compositing, viewport and order. The host still owns Device::beginFrame/endFrame; no extra simulation passes.
- `CameraRenderPipeline::shutdownAfterGpuIdle` also frees compositor shader/buffer and its separate screen RT pool after an externally guaranteed GPU-idle point.
- CPU-only `camera_layout` CTest source was added for layout geometry and multi-screen scheduling.

## Compatibility and limitations
- Old screen-only path retained when no explicit stacked viewport cameras exist. Existing authored camera + RT APIs remain callable.
- Composition currently treats all sources as RGBA8 LDR; HDR screen composites, per-camera MSAA, UI overlay input capture, camera-specific listener selection, GPU capture/movie export and automatic editor viewport binding are not implemented.
- A single screen camera with an explicit partial viewport currently needs compositor handling to preserve its intended rectangle (rather than legacy direct-to-screen). Address this in the next fix before production.
- GPU pipeline/shader layout is unverified on Metal, GL, Vulkan. In particular verify backend orientation, alpha, selection of viewport origin, resize and dynamic quality transitions.
- All source-level ECS components require explicit persistence schema codecs before serialized saves; none were implicitly registered.

## Tests actually executed
- Source review via GitHub; created code commit.
- **NOT RUN**: CMake configure/build, CTest, Metal/GL/Vulkan, hardware image capture, screenshots/editor use, GPU timing, XR. Per user's feature-first request, defer to a consolidated verification run. No CI jobs reported passing.

## Next stage
- Fix single-viewport handling; add validated camera graph ordering and a Scene/Inspector integration path.
- Add cubemap/capture sequence support and GPU readback abstraction, deferred physical effects/portal/XR integration; keep third-party codec/runtime optional.
- Before any merge: full native compilation plus CTest, Metal and GL screenshot comparisons (multi cameras screen+RT/rotated scenes, repeated viewport changes), Vulkan where supported; document exact outcomes in the next handoff.
