# AIVERSE LENS Stage 13 — offscreen texture capture for Metal and GL (2026-11-19)

## Branch / implementation
- `feature/universal-multicamera-render-targets`, implementation `380baa6eab5e80ff54e5cbc685e9ef0450466e46`.
- Modified `engine/gfx/Gfx.h`, `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`, `engine/render/CameraFrameRecorder.h`.
- Previous Stage 12 handoff: `docs/agent-handoffs/universal-multicamera-stage12-export-2026-11-19.md`.
- No main merge / force-push.

## API
- `gfx::Device::captureTexture(TextureHandle source,CaptureFn callback)` is an **optional** virtual API. It is called outside render passes inside an active GPU frame and returns bool whether capture was accepted. Callback receives top-down RGBA8 on the device-owner thread. Default unsupported implementation returns false without callback.
- Metal: implements 8-bit RGBA/BGRA (including sRGB variants) RT capture with a command-buffer blit into a shared buffer, converts BGRA as needed, retains an in-flight capture state, and delivers completed pixel buffers on the following `beginFrame` after GPU completion. It does **not** block normal frame submission to read back the RT.
- GL: implements explicit synchronous `glReadPixels` from a color RT's FBO, with previous framebuffer/pack-state restoration and conversion from bottom-up to top-down RGBA8. This is a debug/export-only call, not for routine per-frame graphics.
- Vulkan: retains the generic unsupported behavior for offscreen texture capture in this stage; presented-screen `captureNextFrame` continues to work as before. Do not claim Vulkan RT export.
- `CameraFrameRecorder::tickTexture(device,texture,time)` schedules one in-flight offscreen capture per recorder with its existing `CameraCapturedFrame` callback, frame index, frequency and `writePng()`.
- HDR RGBA16F RT capture must first be explicitly tone-mapped to an RGBA8 target (not implemented in this stage); do not reinterpret float16 memory as RGBA8.

## Compatibility / risks
- Purely additive virtual method with a default, so existing device subclasses do not require override. C++ ABI changes due to new virtual slot; all native targets must rebuild.
- Metal readback source must remain live through command-buffer completion; `CameraRenderTargetPool` defers retired resources based on confirmed queue serial. Avoid raw `Device::destroyTexture(source)` while readback in flight.
- Backend control behavior must be verified on real AGX/GL hardware: Metal command-encoder ordering, worker callback thread to main beginFrame, sRGB/BGRA orientation, and multiple outstanding captures.
- Vulkan RT capture not yet provided. Optional timeline/async GPU export to RT not production-ready across all backends.

## Tests actually run
- Remote GitHub source inspection and code commit success only.
- **NOT RUN**: CMake, C++/ObjC++ compilation, CTest, GPU tests, Metal runtime, GL runtime, Vulkan runtime, PNG decode, editor/client. User explicitly requested a single consolidated test phase after features.

## Next
- Optional XR/OpenXR view adapters with no hard SDK dependency; capture and geometry/quality budgets, physical exposure and DOF bridging.
- Complete Vulkan offscreen RT readback parity when its queue/fence lifecycle can be safely validated; build and test in one consolidated run before release.
