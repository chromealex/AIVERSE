# AIVERSE LENS + MotionWeave Stage 6 — captured final model packets (2026-11-20)

## Branch, ownership, actual commits
- Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- `b386c5f4825e62202f107b99153c09c39be0cc6f`: `engine/render/Renderer3D.h` non-invasive `ModelPacketTap` callback.
- `f645ee83531d5fcb5a3ae39f89b7b32768af040e`: `engine/render/Renderer3DModels.cpp` invokes tap for finalized `ModelDraw` before culling.
- `5f210bcf06a32b5c05004a873347c2a32278a9e5`, `3668cd5f92963b6c7914c4ac6973c0decaead032`: `engine/render/CameraModelSnapshot.h` owns copies of temporary model submission data, adds testable synchronous visitor.
- `085477e7c49c3c3f2b5e38bb9be253ae8af2cb68`, `cbcc6c6efd6da3ce9b356f5f2fc37c6c8806f53c`, `76d2d5f012bee632e90e6011821c573601dc9885`, `66df69dd3258873420c43d145db37ebd028e2657`: `apps/client/GameClient.{h,cpp}` capture finalized primary-view packets and resubmit through `CameraClientAdapter::renderPrepared`.
- `8733667a7660ddb9a20404fe7f58325c336f3b4e` and `11ff3a1eb367fbb5575b5d53c7b21370a226be74`: `tests/render/camera_model_snapshot.cpp` and CMake `camera_model_snapshot` fixture.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage5-camera-lod-2026-11-20.md`.

## Dataflow / API
1. GameClient's original main camera executes `RtsView::draw` once, which advances MotionWeave, handles visual events and composes final animated models, attachments and legacy model draws. No changes were made to the animation team's RtsView sources.
2. When opt-in LENS cameras are active, `CameraModelSnapshot::beginCapture(r3d_)` installs `Renderer3D::setModelPacketTap` before the primary RtsView draw. This separate tap does NOT replace existing light-baker `setModelCapture`.
3. `Renderer3D::drawModel` calls the tap after validating Model handles, BEFORE primary-camera culling so packets invisible to the primary camera remain capturable for other cameras.
4. `CameraModelSnapshot::record` copies `ModelDraw` values and any borrowed `nodeWorld`, `pose`, `baked`, `materials`, `nodeMaterial` containers. It deliberately clears `staticCache` and transient pointers. Model and GPU resources themselves are still borrowed and must survive until secondary submissions finish.
5. `endCapture` clears tap before the GPU render; extra cameras call `lensCameras_.renderPrepared(snapshot,{simulationTick,incrementingPresentationFrame,time},dt,submit)`. Each draw constructs fresh pointers to snapshot-owned buffers, and invokes normal `Renderer3D::drawModel`. Neither `RtsView::draw` nor `MotionWeave::update` runs a second time.
6. Original screen output stays owned by GameClient. LENS cameras still render available `worldScene_` static geometry and now include finalized model packets.

## Performance and correctness constraints
- Captured packets include authored animated models and attachments passing the primary presenter's draw path; the snapshot replays all of them without repeating events. A bounded 100,000-packet cap drops overflow and exposes `dropped()` (currently no UI metric).
- **Important limitation:** packet capture occurs AFTER the primary camera selects whole-model LODs. Independent camera-scoped LOD exists in MotionWeave View, but the copied final packets currently reflect the primary camera's chosen model/LOD. True independent per-camera model selection requires capturing a pre-LOD object/pose contract and calling `View::submitOne(...,cameraScope)`, not just finalized `ModelDraw`.
- Other scene primitives (sprite batches, terrain/fog, transparent geometry via other renderer submission APIs, GPU particles, decals, trails, lights, UI) are NOT all included. This is a finalized **model** snapshot, not full immutable world render snapshot.
- The capture is host-scoped and synchronous. Avoid concurrent submission, reentrancy or live resource reload between primary and secondary renders. `ModelDraw` points to a borrowed Model; pointer reuse across asset eviction must be guarded by the host's resource-retirement contract.
- `CameraModelSnapshot::beginCapture` installs a callback capturing `this`. GameClient must always call `endCapture`; exceptions from RtsView draw would leave the callback armed until recovered. Add an RAII capture guard before production.
- Multiple primary World and standalone ModelDraw callbacks must not conflict with the light-baker hook. Test on hardware.
- The model snapshot copies material overrides (which can be costly). Benchmark memory/capture overhead with thousands of draws before finalizing.

## Tests prepared but NOT executed
- Added `camera_model_snapshot` CPU fixture: verifies copied containers survive original container clearing, repeatable visit, packet capacity and clearing.
- **NOT RUN:** CMake, C++, CTest, Metal/Vulkan/GLES, Studio or GameClient runtime, direct pixel parity, animation-event invariance. No passing test claims.
- Final consolidated run must test: 1 animation advance and 2+ camera renders, event counts remain constant, animated pose identical at equal presentation time, copied attachments, distinct camera view/frustum, async model reload/retirement, material/texture lifetime, frame shutdown, memory stress.

## Next steps
1. Make a move-safe RAII tap guard, and expose frame capture overflow diagnostics.
2. Implement pre-LOD MotionWeave visual packets per camera with independent view-specific LOD and culling; preserve material and asset-publication lifetime.
3. Expand snapshot beyond models to terrain/sprites/particles/light/fog through an immutable scene-layer provider API.
4. Run one consolidated build/CTest across animation/LENS and backend compile+GPU tests; correct errors, record results, then consider merging to main.
