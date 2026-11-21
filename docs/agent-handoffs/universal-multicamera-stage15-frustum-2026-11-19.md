# AIVERSE LENS Stage 15 — exact off-center camera projection (2026-11-19)

## Branch / commits
- Branch: `feature/universal-multicamera-render-targets`; **not** merged into main.
- Implementation: `978151ad933e7113d3d832146f326d1da3822880` (Math3D.h), `10708cd653f037c74acd85a1ff76e7e22b9a87bf` (CameraXrViews.h).
- Fixture: `c0270146514242037b2831b30f228580303991f4`, CMake: `dfe8f0f0ba687f1d7afc7b4377f7323833d6451f`.
- Changed `engine/render/Math3D.h`, `engine/render/CameraXrViews.h`, `tests/render/camera_frustum.cpp`, `CMakeLists.txt`.
- Previous handoff: `docs/agent-handoffs/universal-multicamera-stage14-xr-2026-11-19.md`.

## API / design
- New `mat4::frustum(l,r,b,t,zNear,zFar,zeroToOne)` right-handed perspective with both OpenGL [-1,1] and Metal/Vulkan [0,1] clip-depth ranges.
- `Camera::setFrustumTangents(left,right,down,up)` validates finite, ordered, origin-spanning slopes and updates the new optional `useFrustumTangents` override atomically; `clearFrustumTangents()` restores legacy projection.
- `Camera::proj(aspect,zeroToOne)` uses exact tangent-based frustum when opted in, independent of framebuffer aspect. Existing jitter and lensShift and oblique clip behavior remain applied afterward. Ordinary cameras keep old perspective and orthographic paths.
- `makeXrCameraViews` now calls `setFrustumTangents` rather than approximating with FOV/lensShift and no longer rejects differing frustum/pixel aspect ratios. Per-eye pose, GPU texture IDs, extent, HDR choice and validation remain SDK-independent.
- No genre-specific dependencies.

## Validation prepared, NOT RUN
- Added and registered CPU fixture `camera_frustum`: validates asymmetric near-plane left/bottom and right/top NDC corners for both clip depth ranges; near/far Z mapping; invalid atomic update; restoring legacy mode; XR eyes with differing output aspect ratios; invalid duplicate targets preserve output.
- Build, CTest, Metal GPU, Vulkan/GLES compilation, runtime XR integration have **not run**. Do not describe the test as passed.
- New `Camera` fields alter its C++ layout: rebuild native targets and watch binary serialization/ABI assumptions.

## Remaining limitations
- Full OpenXR runtime/session, swapchain import and per-eye submission, late-latching/timewarp remain out of scope.
- The XR adapter generates two independent RT views; it does not directly import runtime-owned swapchain images.
- Physical lens and any subsequent camera modifiers that change `fovY` do not override an active frustum. Call `clearFrustumTangents()` when changing back to an ordinary lens workflow.
- Need real backend parity verification for asymmetric clipping with jitter, oblique planes, culling and screen rays.
- Vulkan offscreen RT readback and HDR tone-map/export still pending.
- Main/client/editor validation and genre-independent migration checks still pending.

## Next step
Implement Vulkan offscreen RT capture with explicit staging buffer ownership and GPU fence readiness, without blocking normal frame submission; then integrate HDR tone-map and consolidate CMake/CTest and Metal GPU tests, Vulkan/GLES compile-only checks. Do not merge before validation and correction.
