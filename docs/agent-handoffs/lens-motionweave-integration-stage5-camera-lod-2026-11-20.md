# AIVERSE LENS + MotionWeave Stage 5 — camera-scoped LOD and prepared packet bridge (2026-11-20)

## Branch / code / commits
- Branch: `feature/lens-motionweave-integration`; base retains animation-team universal View and separate LENS headers.
- `1e5934af148b63613aebd9c31720bebf54e23fe8`: `engine/view/View.h` per-camera LOD histories keyed by camera scope (packed ECS entity).
- `6a375e1761f1de18f488af8be195bfaeac40e275`: extracted GPU-independent `selectCameraLodLevel`.
- `d0ae576d39841696567e557d3dd8d0e3875e242c`: `tests/view/camera_lod.cpp`.
- `bd0b8a322185aed422db4a6e0cad5c59bb76be60`: CMake fixture `view_camera_lod`.
- `58ec16cc6fa56aa583442eaee150c438b83c3f6b`: `engine/render/MotionWeaveCameraFrame.h`.

## Architecture
- `View::submitOne(renderer,obj,cameraScope=0)` uses existing single-view LOD history for scope=0, independent per-camera hysteresis for nonzero scope IDs. `submit(renderer,objects,cameraScope=0)` forwards scope.
- `View::selectCameraLodLevel(obj,distance,scope)` updates LOD history only; no animation graph updates, GPU calls or event consumption. A separate camera's hysteresis cannot steer another's.
- Scoped maps are pruned in `update` and `beginPreparedFrame`, cleared on `clear`, and source IDs are erased on forget/reimport; `forgetCameraScope(scope)` supports entity lifecycle. Recycled entities use generation-safe packed keys.
- New borrowed `MotionWeaveCameraFrame` references a previously evaluated generic `view::View` and `span<const view::Object>`. Its `submit` calls `animatedView->submit(renderer,objects,camera.entity.packed())`; `makeMotionWeaveCameraVisualFrame` bridges generic `CameraVisualFrame`.
- Host contract: call `View::update` once per presentation frame, then render N cameras using the same valid borrowed objects and already evaluated poses. No `RtsView::draw` replay. The renderer initiates each camera scene separately.
- Camera-specific LOD is intentionally mutable presentation cache; model pose/animation clocks are not advanced by submit.
- This does NOT automatically capture all RTS scene objects: the RTS adapter must publish a stable immutable packet collection, including material and attachment lifetime, after its one view update. Current `GameClient` still renders scenery-only in its optional LENS pass.

## Test fixture prepared, not executed
- `view_camera_lod` checks independent LOD hysteresis at 2 camera scopes, legacy scope 0, explicit scope eviction and asset publication-generation reset.
- **NOT RUN**: CMake, compiler, CTest, editor/client, Metal/Vulkan/GLES GPU validation; prior stages' tests also pending.
- Check C++ header compile with `MotionWeaveCameraFrame.h` and source lifetime during attachment reimports.

## Next
1. Provide a snapshot export API from RtsView's single MotionWeave `genericView_->update` pass and fallback authored draw path. Avoid copying borrowed graph/model pointers beyond their owner lifetime. If GPU-resource retirement occurs before callback completion, prevent use-after-free.
2. Wire GameClient LENS render callback to MotionWeaveCameraFrame rather than scenery-only rendering.
3. Test one update for N camera renders with unchanged animation playback time and event counts.
4. Run consolidated builds/tests and write measured results. Do not merge into main until validated.
