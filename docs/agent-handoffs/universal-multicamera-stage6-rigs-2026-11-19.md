# AIVERSE LENS — Stage 6: ECS rigs, director, rails, shake and physical lens (2026-11-19)

## Provenance
- Branch: `feature/universal-multicamera-render-targets`.
- Implementation commit: `e7443e7f8172102ea476ed392e013048dc0c2fe7`.
- Files added: `engine/sim/CameraRigComponents.h`, `engine/render/CameraRig.h`, `engine/render/CameraDirector.h`, `tests/render/camera_rig.cpp`; modified `CMakeLists.txt`.
- Preceding stage: `docs/agent-handoffs/universal-multicamera-stage5-rt-pipeline-2026-11-19.md`.
- No changes to `main`.

## Implemented API and architecture
- ECS fixed-point authoring: `CameraRigComponent` supports Static / LookAt / Follow / Orbit / Rail. `target` is generation-checked `aiverse::Entity`, with fixed-point offsets, distance, yaw, pitch and smoothing.
- `CameraRailComponent` stores a fixed maximum 8 scene-space waypoints, duration and loop flag. `CameraShakeComponent` defines wave amplitude/frequency/time ranges; `CameraLensComponent` stores sensor height, focal length, f-number, focus distance, shutter seconds, ISO, and physical-FOV override flag.
- `render::CameraRigEvaluator::apply(world,view,seconds,dt,renderOrigin)` reads `WorldTransform` and optional rig/rail/lens/shake after hierarchy update. Produces independent transient `render::Camera` view poses; smooths with per-entity presentation state; no ECS mutation, floats stay out of authoritative sim. Existing RTS/FPS/Editor hosts remain consumers, not dependencies.
- `render::cameraLookRotation` uses local -Z forward, +Y up; `cameraRailPosition` interpolates authored points on absolute presentation time, supporting seek/loop. `render::cameraPhysicalLens` exposes metadata for future DOF/exposure; physical vertical FOV calculated from sensor height and focal length.
- `render::CameraDirector` selects highest-priority screen camera with stable Entity tie-break, tracks active Entity, interpolates camera pose/FOV/clip/ortho size (ease in/out), accepts explicit cuts and blendDuration; exposes `CameraDirectorFrame`. Does not mutate ECS and is not a separate camera identity registry.
- No pre-existing code or save formats changed; added optional components only. Disk persistence requires explicit PersistentEcs stable schema registration/codec before use.

## Tests (deferred by user)
- Added CPU-only CTest `camera_rig` with target `aiverse_camera_rig_tests`. Cases: Follow pose, physical-lens FOV, rail interpolation, director priority and blend/cut.
- **NOT RUN**: CMake, compilation, CTest, Metal/GL/Vulkan, editor smoke, GPU pixel tests, performance/VRAM, networking and persistence. Per explicit user instruction run tests **once** after feature work; don't claim pass.
- Risks: The rig evaluator does not itself wire into the client/editor main loop, nor does it provide collision-based spring arm, advanced Catmull-Rom rail easing or bone-attachment targeting. A separate scene/host integration is needed.
- The camera director is a presentation solution; binding it to physical split-screen composition and clip/variant reset remains pending. Physical lens has not been wired to DOF/exposure shader implementation.
- This stage is **source committed, not compile validated**.

## Immediate next stage
- Provide per-ECS-camera PostFX/temporal state and a frame-level modifier hook in `CameraRenderPipeline`, so rigs, Director, and FOV are applied to the view passed to `Renderer3D`.
- Implement viewport composition/split-screen over offscreen color RTs with clean load/clear semantics (do not use two independent screen beginPass clears). Add stage code commit and separately commit a full handoff.
- Continue hardware-independent capture/cubemap/portals/VR abstractions later; no optional OpenXR/TinyEXR/FFmpeg runtime dependency by default.
