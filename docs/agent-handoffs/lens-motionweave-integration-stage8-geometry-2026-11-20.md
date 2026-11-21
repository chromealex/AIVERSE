# AIVERSE LENS + MotionWeave Stage 8 — camera geometry packet snapshot (2026-11-20)

## Git provenance
- Branch `feature/lens-motionweave-integration`, no main merge.
- `396dee0ac8aa9b3d19e06ba579347f0c8105f193`: `engine/render/Renderer3D.h`, genre-neutral `CameraGeometryCommand` and optional `GeometryPacketTap`.
- `1411f5ea47d275ce9955b1ed47c9333e0ed9e221`: `engine/render/Renderer3D.cpp`, capture ordinary mesh, line and triangle submissions.
- `7846f6fd8ab486a760eebf518e810aa2c319f26d`: `engine/render/CameraModelSnapshot.h`, bounded, ordered geometry packet recording + per-camera replay.
- `f694c0a4e30cbc396c84af60ad97b56bafeb12c7`: `tests/render/camera_model_snapshot.cpp`, value ownership/order fixture.
- Latest animation sync: `c3c1f943a8f4cda8a52d2c0acdaa1818455bcbc5`, two-parent merge of animation `7f8f04733bf1f0db77b08b207e29c358cb0d1476` (atomic ModelView reimport). Previous animation base `5a32df53c55ea18ab7d4af350a0b054dbe3c6e7e`; only new `apps/modelview/main.cpp` source differed and no conflict with LENS.
- Prior handoff `docs/agent-handoffs/lens-motionweave-integration-stage7-prelod-2026-11-20.md`.

## Architecture / API
- `CameraGeometryCommand` is a value type with enum `Mesh`, `Line`, `Triangle` and authored transforms, colors and flags. Mesh pointer is borrowed from owning host and requires matching GPU resource retirement.
- `Renderer3D::setGeometryPacketTap` is independent from the model capture tap and static light-baker capture. `drawMesh`, `line`, `triangle` each record their submitted geometry when enabled, without reentering scene/simulation/animation systems.
- `CameraModelSnapshot::beginCapture` now installs both geometry and model taps; its RAII `CaptureGuard::end` clears both. A single primary `RtsView::draw` fills both streams.
- `CameraModelSnapshot::recordGeometry`, `geometryCount`, `forEachGeometry` own the parameter copies, preserving insertion order **within the geometry stream**, with a bounded maximum; `submit(renderer,cameraScope)` emits captured simple geometry then fallback models and scoped MotionWeave packets.
- The current GameClient Stage 7 `renderPrepared` pathway automatically reuses these packets in extra ECS cameras without another call to `RtsView::draw`, `MotionWeave::update` or `handleEvents`.
- Original primary camera still uses its normal Render3D pipeline.

## Scope / important limits
- This is a **basic geometry packet** capture, not all terrain/fog/lighting/VFX/2D sprite systems. Only subpasses already emitting `Renderer3D::drawMesh/line/triangle` while capture is active are included. RtsView's early streamed-world draw happens before capture and is already independently submitted from `worldScene_`; dedicated terrain, water and GPU sprite/particle renderers still need explicit integration.
- `drawNereidRippleMesh` special water flags are excluded from the plain mesh tap, because its ripple/wake and hydraulic data cannot be expressed correctly with the base mesh packet. A full water capture packet must preserve its extra material state.
- `drawSelectionMesh` uses `drawMesh` and a post-call selection override; current geometry snapshot captures the plain mesh, not the extra selection outline attribute. Camera-facing tracers/line billboards baked for the primary camera are not yet recomputed per secondary viewpoint.
- Mixed geometry/model ordering is not exactly preserved (geometry is replayed ahead of model streams); opacity/ordering and missing secondary-camera-specific layers require pixel parity tests.
- This capture is per-frame and holds borrowed `Mesh*` pointers. No asynchronous reuse across asset reload, device shutdown or GPU frame boundaries. Memory budget/stress tests outstanding.
- Render submission needs backend shader/projection verification for Metal, Vulkan and GLES.

## Actual tests and verification
- GitHub source inspection; targeted file edits, Git-object SHA source sync merge; no conflicting animation paths.
- **NOT RUN:** CMake, compilation, CTest, Studio/GameClient, GPU tests, Vulkan/GLES build. Runtime container has no AIVERSE checkout and GitHub network resolution fails; only connected GitHub file tools were available.
- Extended existing `camera_model_snapshot` CPU test with geometry value/ordering checks; fixture written but not executed.
- Do not call Stage 8 fully complete/working on hardware until a validated build/test run.

## Next stage
1. Add explicit prepared packet capture for camera-independent terrain layers, fog material data, sprites and particles that do not go through `Renderer3D` primitive methods.
2. Support special water / selection packets without dropping per-pass material parameters.
3. Preserve cross-model/geometry transparent render order and per-camera billboard orientation.
4. Consolidated compiler/CTest/GPU pass, correct errors, then decide on merging into main.
