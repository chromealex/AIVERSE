# AIVERSE LENS Stage 16 — Vulkan offscreen RGBA8 RT readback (2026-11-19)

## Branch / implementation
- Branch `feature/universal-multicamera-render-targets`; main not changed.
- Commits: `afbad9ddd792cc8fa5ebe15ea18614b72c243a72` (VkDevice.h), `c6512913b8e7762761094b32ed3309919ac07fd3` (GfxVulkan.cpp).
- Changed files: `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/GfxVulkan.cpp`.
- Previous handoff: `docs/agent-handoffs/universal-multicamera-stage15-frustum-2026-11-19.md`.
- Genre-independent backend implementation; no RTS/FPS dependencies.

## Behavior and API
- VulkanDevice now overrides `gfx::Device::captureTexture(TextureHandle,CaptureFn)`.
- Accepts color render targets in `RGBA8` or `RGBA8_sRGB`, 1..8192 pixels in each dimension; refuses unsupported/depth/HDR formats.
- Valid only inside an active frame, outside passes. No callback is fired if the method rejects a request.
- A host-coherent transfer-destination staging buffer is allocated per request. The source color image is transitioned SHADER_READ_ONLY -> TRANSFER_SRC -> SHADER_READ_ONLY around `vkCmdCopyImageToBuffer` on the current main command buffer.
- Per-slot `FrameSlot::textureCaptures` retains the buffer, allocation and callback until the slot fence is successfully waited in `prepareSlot`. This means callback delivery is **deferred until frame-slot recycling**, not necessarily the first available GPU completion. There is no explicit blocking wait solely for offscreen RT capture, unlike legacy `captureNextFrame`.
- On confirmed fence completion, copies mapped bottom-up Vulkan rows to top-down RGBA8 vector, destroys capture buffer, releases allocator block, then invokes callback on Device owner thread.
- Device teardown waits GPU idle and frees any pending capture resources without dispatching callbacks. On unsuccessful fence wait, the slot is not safely recycled by this path; this follows pre-existing error model and needs device-loss stress checks.
- Source texture destruction uses existing `deferDestroy` at current serial so queued copies remain GPU-safe when the host retires source after capture.

## Important limitations / audit notes
- Not yet compiled or GPU validated. Do not describe implementation as production-ready.
- Only RGBA8/sRGB; HDR RGBA16F requires a separate tone-map resolve to an RGBA8 target. No direct EXR.
- Callback is delivered only when slot is recycled; if rendering stops and the device stays alive, pending readback may remain pending until another frame. A future explicit poll/drain method should be considered.
- Vulkan render-target row orientation is assumed bottom-up per the RHI convention, but requires a colored-quadrant GPU parity test against Metal/GL.
- Existing Vulkan `captureNextFrame` remains synchronous at endFrame and is separate from this offscreen path.
- Investigate device-loss teardown behavior, allocation failure and callbacks that destroy the device or attempt reentrant GPU calls.
- Vulkan RHI portability still requires compilation with Vulkan SDK on a supported platform.

## Actual tests
- GitHub writes completed; **NO** CMake, CTest, Clang, Vulkan build, Vulkan runtime, Metal/GL tests were executed in this stage. This is intentional until consolidated validation.
- Suggested future tests: RGBA8 and sRGB pixel parity; top-down orientation using four distinct corners; two captures in same frame; retire source after scheduling; reject HDR/depth/out-of-frame; resize during in-flight callback; multiple frames; failure injection and device loss.

## Next
- Stage 17: implement explicit HDR-to-SDR color conversion and PNG export path; support linear and exposure semantics correctly across Metal/GL/Vulkan without misinterpreting RGBA16F bytes.
- Complete missing editor/client end-to-end camera capture integration.
- Run full CMake/CTest and GPU tests as one consolidated pass; compile-only checks for Vulkan and GLES; fix errors and write validation handoff before merging.
