# AIVERSE LENS / MotionWeave Stage 7 — pre-LOD packets and hybrid camera replay (2026-11-20)

## Provenance
- Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- Latest animation sync merge: `d81881693db6f13357a6085bfde590dfa29c8937` (animation parent `ff2d5e7d137343fe633398e628732a0a91154216`), zero overlapping paths.
- Fixed MotionWeave source identity at submission: `35e8c4a0848b934b6297a14ffeae0210a6cf14b2`.
- Pre-LOD snapshot source: `5e89f97ca66d05e6e042a77743ee0176c8ec6b69`, `1eebaa761bfed296431928f77277895cbfb2dc0b` in `engine/render/CameraModelSnapshot.h`.
- Optional RtsView export hook: `e3fe2aef6357ca752d9948aa5f7804a38be12ca5` in `modules/rts/view/RtsView.h`; capture before primary submission: `22c1f9250a2f767ff34de4d216b79298a32f866f` in `RtsViewDraw.cpp`.
- GameClient scoped borrow + camera-specific replay: `1ac91fd41008998bcfc814bfed436fc7be21189c`.
- Test additions: `dc81afb50251a0e52d2393a8f9b2b96dc35c194e` in `tests/render/camera_model_snapshot.cpp`; CMake dependency `4b9548efd9d12750f19a8d626238ba351d5872fe`.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage6b-capture-raii-2026-11-20.md`.

## Architecture and API
1. MotionWeave's `RtsView::draw` still invokes its generic `View::update()` **once** during the original GameClient main-camera presentation. Secondary camera passes never run this call.
2. The generic RtsView adapter in `RtsViewDraw.cpp` now fills `view::Object::generation` and `animationModelGeneration` with the same source metadata it passed to `View::update`. Previously both defaulted to zero during `submitOne`, potentially bypassing evaluated graph poses. The fix is needed independently of LENS.
3. `RtsView::setCameraPreparedCapture(render::CameraModelSnapshot*)` is an optional, one-frame borrowed capture pointer. GameClient sets this only while LENS is active and uses an on-stack cleanup guard for exceptional exits. `drawObject` calls `snapshot.recordPrepared(*genericView_,packet)` before `genericView_->submitOne` for eligible generic animation objects.
4. `CameraModelSnapshot` keeps existing finalized fallback `ModelDraw` packets, and additionally owns a copy of the pre-LOD `view::Object` metadata and all transient `ModelDraw` overrides (nodeWorld, pose, materials, nodeMaterial, baked). The originating universal `View` is borrowed for the remainder of the frame.
5. Captured final base packets matching a prepared object's stable `persistentId` are suppressed during secondary submissions; attachments and other legacy model draws continue through the finalized fallback list. The original screen rendering is unchanged.
6. Each secondary ECS camera calls `CameraModelSnapshot::submit(renderer,camera.entity.packed())`. Prepared packets are resubmitted through MotionWeave `View::submitOne(renderer,packet,cameraScope)` with isolated per-camera LOD hysteresis, and the fallback models are sent through `Renderer3D::drawModel`. No animation graph update, event queue consumption or legacy RtsView replay is invoked.
7. New helper APIs `isPreparedDraw`, `forEachPrepared`, `forEachFallback` allow CPU verification without opening a graphics device.

## Scope and limitations
- This applies to the subset of RTS actors already using the universal MotionWeave View path. Legacy rigs, terrain, foliage renderers, 2D sprites, particles, GPU scene layers and UI remain fallback/uncaptured where not emitted as `ModelDraw`.
- The existing RTS generic-View eligibility gate explicitly excludes `look.lods`, complex procedural rigs, attachments and contact-heavy graphs. Thus **camera-scoped LOD history is correct for prepared objects that expose View LODs**, but current RTS generic adapter does not yet populate whole-model LOD lists. Do NOT claim all RTS model LODs independently select by camera.
- Attachment draws from the legacy adapter stay finalized, frozen and in the fallback stream. Native View packets with their own attachments can use View-scoped submission when provided by that host.
- `View::submitOne` has mutable per-camera hysteresis and scratch counters, but its graph pose is already evaluated; it is not a fully immutable renderer internals API.
- Source `Model*`, GPU buffers, textures and the originating `View*` are borrowed; they must outlive all offscreen camera submits. The copied per-frame material/pose containers do **not** own GPU resources.
- Shader/Material and RHI backend integration changes from earlier stages still need native build verification.
- Fallback vs prepared packet order is not preserved exactly across the two categories. The renderer's pass sorting may cover some opaque cases, but transparent/overlay ordering needs pixel comparison before shipping.

## Tests authored; exact execution status
- Extended `camera_model_snapshot` CPU fixture to cover pre-LOD packet storage, deep copy of transient materials, base-draw suppression, fallback attachment preservation and clearing.
- `camera_model_snapshot` CMake target now links the MotionWeave View interface and compression dependencies.
- **NOT RUN**: compiler, CMake, CTest, GPU, editor, GameClient or frame parity smoke. No passing test claims.
- Required acceptance: 1 animation update vs 2+ camera submissions, no change to event/FX counts, correct pose vs MotionWeave graph, no duplicate base packets, correct attachment lifetime, independent LOD near/far and hysteresis, transparency order, reload races, Metal/Vulkan/GLES GPU, final memory benchmark.

## Next
1. Refactor generic View camera packet staging to include per-camera whole-model LOD source data from authored objects, where compatible with MotionWeave source skeleton/retarget generation.
2. Expand scene capture to non-model layers (terrain, sprites, particles, lighting and fog) via a read-only visual frame.
3. Complete remaining branch conflicts/rebase to moving animation HEAD and perform ONE consolidated compilation/CTest/GPU pass before merge to `main`.
