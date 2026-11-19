# AIVERSE LENS Stage 9 — capture cameras, cubemap faces, stereo, mirror, portals (2026-11-19)

## Branch / implementation
- Branch `feature/universal-multicamera-render-targets`; implementation commit `a11828377a28fb58c6b3679154c6e6672464c895`.
- Paths: `engine/render/CameraCaptureViews.h` (new), `engine/render/Math3D.h`, `engine/render/CameraRenderPipeline.h`, `tests/render/camera_capture_views.cpp` (new), `CMakeLists.txt`.
- Previous handoff: `docs/agent-handoffs/universal-multicamera-stage8-compositor-2026-11-19.md`. No main merge.

## Implemented
- `render::Camera::lensShift` adds optical projection shifts to perspective and orthographic projection (supports asymmetric stereo/off-axis lens).
- `render::Camera::setPose` now also derives `target` from the orientation and distance so legacy shadow, lighting and other target-based computations don't retain a stale orbit point.
- `makeCubemapCameraViews(source,keys[6],faceSize,hdr)` returns six 90-degree view poses (+X,-X,+Y,-Y,+Z,-Z) with independent target texture keys, no jitter.
- `makeStereoCameraViews(source,IPD,convergence,keys[2],w,h)` returns two parallel eye poses offset by the camera's right vector with opposing lens shifts (no toe-in).
- `makeMirrorCamera(source,normal,offset)` reflects camera across a plane; `makePortalCamera(source,entrance,exit)` transforms camera from entrance to exit coordinate frame. Up-vector/roll preserved through matrix basis to normalized quaternion.
- `CameraRenderPipeline::renderCaptures(vector<CameraCaptureView>,submit)` encodes offscreen capture views using existing target pool and Renderer3D; host supplies scene submissions. Target textures are available via `pipeline.targets().color(assetKey)` after successful encoding.
- Fixed Stage 8: a single partial viewport screen camera now uses the compositor path as well; it no longer draws over the entire swapchain.
- Authored CPU test `camera_capture_views`.

## Explicit limits
- Six separate 2D render targets are **NOT** yet a hardware cube texture. An equirectangular panorama compositor is a future stage.
- Planar reflection and portal transforms do **not** by themselves provide oblique clipping, stencil masking or recursion-safe dependency management; host must apply those constraints before claiming full portals/mirrors.
- No OpenXR runtime, raw RT-to-CPU readback, PNG/EXR encoder, video streaming/recording or render movie timeline yet. `Device::captureNextFrame` works for presented screen frames, not arbitrary offscreen RT pixels.
- Persistent/serialized camera components still require explicit registered codecs. No genre-specific dependency added.
- FPS/RTS/Studio runtime scenes aren't yet auto-wired into the multi-camera scheduler. New capture APIs are native C++ opt-in.

## Tests executed
- GitHub connector source inspection and commit creation only.
- **NOT RUN**: configure, native compilation, CTest, Metal/GL/Vulkan, XR hardware or UI screenshot parity. Per owner's request, run tests once after finishing all features and record actual diagnostics.
- Suggested consolidated targeted runner: `ctest --test-dir build/lens -R '^(ecs_camera|camera_scheduler|camera_rt_pool|camera_rig|camera_temporal|camera_layout|camera_capture_views)$' --output-on-failure`.

## Next agent action
1. Add oblique clip plane and recursion-aware camera target scheduling; wire mirror/portal views into scene surfaces (not just camera pose functions).
2. Add cubemap-to-panorama GPU shader/readback export pathways, optional OpenXR bridge and camera capture sequence/recording policies.
3. Integrate CameraEntity authoring/selection into Studio and real native clients; validate per-camera transient jitter, screen and multi-pass stability after the final feature pass.
4. For each bounded stage, code commit then separate handoff commit on this branch; no force pushes, no premature main merge.
