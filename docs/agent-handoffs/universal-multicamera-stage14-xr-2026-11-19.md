# AIVERSE LENS Stage 14 — XR stereo view adapter (2026-11-19)

## Branch and implementation
- Branch: `feature/universal-multicamera-render-targets`
- Code commit: `ea0715ef493e6c41a22d52e29dc12cde8528a208`
- Added `engine/render/CameraXrViews.h`; this handoff is a separate documentation commit.
- Continues Stage 13: `docs/agent-handoffs/universal-multicamera-stage13-readback-2026-11-19.md`.
- Genre-independent; no RTS/FPS dependencies. No merge into main.

## API and behavior
- `CameraXrEye`: predicted world pose, signed visible frustum tangents, target asset key, pixel extent, HDR flag.
- `CameraXrFrame`: two runtime-provided eyes and predicted presentation timestamp.
- `makeXrCameraViews(frame,cameraTemplate,output)` converts both eyes to the existing `CameraCaptureView` objects and outputs per-eye Camera poses, FOV, and off-center lens shifts.
- This is a pure CPU adapter. It validates finite numbers, nonzero quaternion, distinct nonzero targets, positive extents, valid frusta, camera near/far, and projection tangents matching pixel aspect within 1%. It clears temporal jitter and oblique clip. On error it returns false without changing the caller's output.
- Frames/poses are expected in AIVERSE's right-handed +Y up, -Z forward convention; external OpenXR host must perform conversion if necessary.
- Off-center perspective is represented using the existing `Camera::fovY` and `Camera::lensShift`. No XR SDK is required to compile this header.

## Explicit limitations
- This is **not** a complete OpenXR implementation: no session lifecycle, swapchain interop/acquisition, runtime extension negotiation, multiview submission, timewarp/late-latching, hand tracking, depth composition, or controller input.
- `Camera::proj()` currently uses framebuffer aspect, so runtime eye tangents whose aspect differs materially from target image aspect are rejected rather than distorted. A general 4-plane projection override would be a follow-up enhancement.
- Eye pose is accepted as predicted pose supplied by the host; this layer does not predict tracking.
- No claims of runtime parity across Metal, Vulkan or GLES.

## Tests actually run
- GitHub file write succeeded; **no** CMake build, compile, CTest or GPU execution performed in this stage. This follows the requested consolidated test pass after completing features.
- During consolidated validation add CPU tests for symmetric/asymmetric views, invalid frame atomicity, finite-angle guard, per-eye target uniqueness and basis projection parity.

## Next steps
1. Add a generic asymmetric-frustum projection to the Camera API to handle arbitrary XR tan-bound combinations without pixel-aspect rejection, with backend parity checks.
2. Implement safe Vulkan offscreen RT readback with fence/device lifecycle and HDR tonemapping/export.
3. Bridge Studio/client ECS camera serialization and editor UI to all advanced LENS capture paths.
4. Run one full CMake/CTest + Metal and build-only Vulkan/GLES validation; write actual results in a new handoff and address failures before merging.
