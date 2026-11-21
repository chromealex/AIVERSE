# AIVERSE LENS Stage 12 — PNG image sequence and panorama output (2026-11-19)

## Branch / files / code commit
- `feature/universal-multicamera-render-targets`, implementation SHA `78915bf2049a5012bfc539a9489b51cff6c635d7`.
- Added `engine/render/CameraPng.h`, `CameraFrameRecorder.h`, `CameraPanorama.h`, `tests/render/camera_png.cpp`. Extended `engine/render/CameraRenderPipeline.h`, `CMakeLists.txt`.
- Parent handoff Stage 11: `docs/agent-handoffs/universal-multicamera-stage11-portal-2026-11-19.md`.
- No main merge; all source edits confined to branch.

## Implemented API
- `cameraEncodePngRgba8(width,height,rgba,bytes)` encodes top-down RGBA8 pixels as valid PNG using IHDR/IDAT/IEND, uncompressed DEFLATE stored blocks inside a zlib stream, CRC32/Adler32 and strict size validation (<=8192/side). `cameraWritePngRgba8(path,...)` writes the encoded PNG. No external codecs needed, but files are larger than compressed PNG.
- `CameraFrameRecorder`: `start(fps,callback)`, `tick(device,time)`, `stop`. One outstanding async `gfx::Device::captureNextFrame` at a time; callback receives `CameraCapturedFrame{index,time,width,height,rgba}`, with `writePng(path)` convenience. Weak-pointer state guards callbacks after stop/destruction. Captures the **presented screen frame**, not arbitrary offscreen RT.
- `CameraPanorama`: six previously rendered 2D cubemap face `TextureHandle`s (+X,-X,+Y,-Y,+Z,-Z) to equirectangular output RT via GLSL/MSL fullscreen pass, no CPU copy. Uses device backend flip metadata for correct sampling orientation. `CameraRenderPipeline::renderPanorama(faceKeys,outputKey,width,height,hdr)` looks up face RTs from existing target pool, allocates output RT and marks it ready on reported healthy device submission. Pixel algorithms must still be verified on hardware.
- New CPU-only `camera_png` test fixture covering PNG signature/IHDR/IEND and invalid inputs.

## Explicit omissions and safety
- No generic RT->CPU async readback yet; `captureNextFrame` is screen-only. To write arbitrary RT/panorama images to files, add a backend-specific readback bridge with queue-completion lifetime and top-down RGBA output.
- PNG output is RGBA8 SDR; HDR EXR writing, video encoding, GIF/movie timeline and OpenXR runtime still pending. FFmpeg, TinyEXR and OpenXR remain optional external integrations and do not block ordinary builds.
- Panorama source direction and face UV conventions are plausible but **not GPU pixel-validated**. The face Y flip depends on Gfx device backend behavior; compare all six directions with known landmarks before production.
- Native project host must own the frame lifecycle, schedule screenshots and choose file paths. FrameRecorder's capture callback may return zero-sized failed data; check `CameraCapturedFrame::valid()` before writing.
- A separate `CameraPanorama::shutdownAfterGpuIdle` is invoked through pipeline shutdown after a host-confirmed GPU idle.

## Actual tests
- GitHub source edit/commit only.
- **NOT RUN:** CMake, CTest, Metal/GL/Vulkan, screenshot decode/visual inspection, SDK integration, release packaging. User explicitly requested features before one consolidated test run.
- Later target: `aiverse_camera_png_tests`, CTest `camera_png` alongside all earlier `camera_*` cases; test actual PNG with a decoder and panorama face-direction parity.

## Next stage
- Add optional OpenXR view/predicted-pose adapters and a safe portable RT readback capability contract (implement backend paths where supported rather than promising unsupported output).
- Finish lifecycle and ECS camera persistence/Studio game-client binding; then one thorough test batch and corrective commits with actual results.
