# AIVERSE LENS Stage 11 — Oblique projection and capture dependency graph (2026-11-19)

## Branch / commits
- Branch `feature/universal-multicamera-render-targets`.
- Code: `5a30548dcce4f15cd29ff9d00699ed3cbd219dd7`.
- Files: `engine/render/Math3D.h`, new `engine/render/CameraCaptureGraph.h`, `engine/render/CameraRenderPipeline.h`, `tests/render/camera_scene_asset.cpp` (fixed JSON mutation source), new `tests/render/camera_capture_graph.cpp`, `tests/render/camera_oblique.cpp`, `CMakeLists.txt`.
- Previous handoff Stage 10: `docs/agent-handoffs/universal-multicamera-stage10-editor-2026-11-19.md`.
- No changes to main.

## Functionality
- `render::Camera` adds explicit `obliqueClip` and `clipWorldPlane`. `proj(aspect,zeroToOne)` adjusts projection row 2 from world-space clipping plane transformed by inverse view transpose, supporting both GL (-1..1) and Metal/Vulkan (0..1) clip depth conventions. Existing behavior is unchanged with `obliqueClip=false`.
- `CameraCaptureNode` declares offscreen capture key and up to eight RT dependencies, recursion depth. `makeCameraCaptureGraphPlan(nodes,maxDepth)` checks invalid/duplicate outputs, self reads, missing dependencies, dependency cycles and max recursion before scheduling a topologically sorted list of capture views.
- `CameraRenderPipeline::renderCaptureGraph(nodes,submit,maxDepth)` renders valid ordered captures through the existing `renderCaptures` path. Material sampling must still declare dependencies; this is not introspected from shader graphs.
- Fixed `tests/render/camera_scene_asset.cpp` source to use `Json::find(...)->at(0)` for mutable JSON rather than mutating through a const `operator[]`.
- Added CPU-only CTest `camera_capture_graph` and `camera_oblique` sources.

## Compatibility and limits
- No ECS save format or network protocol change. `render::Camera` source/binary layout extended (no ABI promise).
- Oblique clip must use a correctly oriented world-plane and physically appropriate epsilon/bias; this stage does not automatically derive exit portal plane or clip masks/stencil for material surfaces.
- Portal recursion is explicitly **bounded and DAG-scheduled**, not an unrestricted recursive renderer. User code must supply the portal scene geometry/material, framebuffer target, and the actual capture requests.
- One-shot/render sequence encoding, runtime OpenXR view acquisition, GPU RT readback and PNG/EXR/movie export are still separate missing features.
- Portal/mirror/reflection visual parity unverified. Asymmetric projection + oblique near plane must be tested with actual GL/Metal and Vulkan depth conventions.

## Tests actually performed
- Remote code inspection and GitHub commit only.
- **NOT RUN:** native C++ compilation, CTest, GPU, editor, screenshot, profiling (user requested one test run after features). Tests were authored but have no pass result.
- Suggested later: CTest `camera_capture_graph|camera_oblique|camera_scene_asset`; inspect world-plane normals and exact pixel clipping.

## Next step
- Add capture screenshot/sequence export (presented framebuffer + optional RT readback backend) and cubemap-to-panorama conversion; wire OpenXR adapter conditionally behind optional SDK dependency, no mandatory runtime SDK.
- Then run **one full test pass** with all LENS tests and real GPU viewport, RT, portal, stereo, temporal and project reload checks; fix failures before merge and update handoff with results.
