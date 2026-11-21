# AIVERSE LENS Stage 17 — HDR-to-SDR RT resolve and PNG capture path (2026-11-19)

## Branch, implementation, handoff
- Branch: `feature/universal-multicamera-render-targets`; no main merge.
- Code commits: `5c148b4502a86e78d00e15171082d274ba534daa`, `d2d2784c4d26206495a971e20ef7a83f363a71c6`, `d609881efa375a062dc27b2a7fa500f80108c97e`, `4441246bcb3095e74ddd961fa6493fff689107c6`.
- Test/CMake: `ebd5705903b4373800d1b87009b987ec3bb37877`, `95f0684d382d7b3526ec0b4bb1f51cdbd018429c`.
- Added `engine/render/CameraHdrResolve.h`, `tests/render/camera_hdr_resolve.cpp`; modified `engine/render/CameraRenderPipeline.h`, `CMakeLists.txt`.
- Previous handoff: `docs/agent-handoffs/universal-multicamera-stage16-vulkan-readback-2026-11-19.md`.

## API and dataflow
- `CameraHdrResolveSettings{exposureEV}` finite exposure constrained to [-16,16] EV.
- `CameraHdrResolve::init(device)`, `render(hdrSource,sdrTarget,width,height,settings)`, `shutdownAfterGpuIdle()`.
- The resolution pass samples linear RGBA16F input; applies `2^exposureEV`, channel-wise Reinhard `x/(1+x)`, sRGB transfer and clamps alpha; outputs display-ready RGB in ordinary `RGBA8` **UNORM** RT, not `RGBA8_sRGB`, to avoid double encoding.
- GLSL and MSL fullscreen passes use existing RHI shader/pipeline/quad conventions. GLES, Vulkan and Metal share GLSL/MSL source through the existing shader compiler path.
- GPU sampling orientation uses `DeviceInfo::renderTargetFlipY` exactly like the existing LENS camera compositor.
- `CameraRenderPipeline::resolveHdrForExport(hdrKey,sdrKey,settings)` checks source ready+HDR, allocates distinct SDR RT via pool, executes resolve, and marks SDR valid on successful device submission.
- `CameraRenderPipeline::recordHdrFrameForPng(recorder,hdrKey,sdrKey,seconds,settings)` optionally resolves and requests `CameraFrameRecorder::tickTexture`. The recorder's existing callback returns `CameraCapturedFrame` for host PNG writing via `writePng(path)`.
- The path is explicitly `HDR scene -> tone-map SDR RT -> backend offscreen RGBA8 readback -> top-down PNG`. A native half-float HDR export/EXR file is **not** implemented in this stage.
- Shaders disable depth test/write, do not sample their own destination, and have one full-screen triangle. No genre-specific dependencies.

## Validation prepared, NOT RUN
- New CTest `camera_hdr_resolve`: validates exposure range/NaN, black/negative clamp, 0/±1 EV reference values, and high-luminance monotonicity; CPU reference `cameraHdrToSrgb()` is in `CameraHdrResolve.h`.
- **NOT RUN:** CMake configure, C++ compilation, GPU shader compilation, CTest, Metal/Vulkan/GLES runtime. Do not call the test passed.
- Final validation must include GPU pixel parity vs CPU reference for all three backends, sRGB/gamma double-encode checks, flip orientation, alpha preservation, color-space assumptions (scene-linear vs already tone-mapped), shader compiler compatibility, repeated captures/resizes/fence correctness, and PNG decode.

## Constraints / risks
- Only an SDR PNG export view of HDR content. EXR/native HDR, PQ/HLG, ACES/AgX, auto-exposure and display metadata require separate features.
- Existing PostFx may already tone map; do not apply this resolve twice. Host must provide true scene-linear HDR RT.
- Uses per-channel Reinhard as deliberate simple baseline; perceptual color/art direction may need subsequent operator selection.
- Renderer source must remain allocated until GPU submission completes; target pool handles retirement.
- `recordHdrFrameForPng` returns scheduling status, not completed PNG file status.
- `CameraHdrResolve::shutdownAfterGpuIdle` requires confirmed GPU idle, as for other LENS fullscreen GPU resources.

## Next step
1. Audit and integrate Studio/client UI and ECS camera serialization with HDR capture controls.
2. Decide on optional native HDR EXR export and advanced color grading as separate stages.
3. Run the consolidated CMake/CTest GPU tests and Vulkan/GLES compile-only checks, fix discovered issues before release.
