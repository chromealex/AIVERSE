# AIVERSE LENS + MotionWeave integration Stage 4 — visual frame seam (2026-11-20)

## Branch / code
- Branch: `feature/lens-motionweave-integration`; main unchanged.
- Prior integration handoff: `docs/agent-handoffs/lens-motionweave-integration-stage3-2026-11-20.md`.
- Implementation: `bf5297c8df3b3bbe1a68984338900f6e398407a8`.
- Changed `engine/render/CameraClientAdapter.h`; added generic `renderPrepared(frame,CameraVisualFrameId,dt,submit,origin)` route over the existing immutable `CameraVisualFrame` contract.
- This API reuses the same caller-owned const frame for every ECS camera and routes each to the existing scheduler/renderer. It is presentation-only by contract; the callback is application-defined and may still mutate outside state if misused.

## MotionWeave interaction — source audit
- The animation branch's `engine/view/View.h` already supports evaluating animation with `update(...,dt)` and submitting existing poses through `submitOne(renderer,obj) const`.
- `submitOne` does not explicitly advance the animation graph. HOWEVER, its mutable `lodState_[obj.id]` and retarget performance counters/scratch are modified by submissions.
- It therefore cannot currently be called from N camera passes using one shared View instance with camera-independent LOD history without risking cross-camera LOD hysteresis contamination.
- Proper integration should: (a) snapshot stable immutable Object/model/pose and material ownership after the one animation update, (b) use a per-camera LOD state map keyed by camera generation-safe identity + model identity, and (c) submit prepared data to N cameras without firing events or updating animation clocks.
- `RtsView::draw` remains stateful; do not replay it for secondary cameras. Existing client integration deliberately draws streamed scenery only until packet snapshot is implemented.

## Backend and client status
- Stage 3 transferred Metal, GL/GLES, Vulkan offscreen capture implementations and GPU-idle hooks, plus editor shutdown and opt-in GameClient RT camera integration.
- These paths were written remotely to the branch; no verified compilation. Restore/retire device lifetimes must be tested with real hardware.
- Animation source `feature/universal-ecs-animation-pipeline` continued moving during work; refresh its HEAD before final reconciliation.

## Tests actually performed
- GitHub inspection and source commits only.
- NOT RUN: native Git merge, CMake configuration, C++ build, CTest, Metal/Vulkan/GL GPU integration, game/editor smoke tests.
- Do not treat this stage as a production-ready multi-camera animation system.

## Next
1. Add per-camera LOD state isolation to generic View or a frozen View render adapter, preserving current single-camera API.
2. Produce immutable per-frame packets with asset publication generations and safe pose/material ownership.
3. Add tests verifying update once, N submissions, no duplicate events/animation time, independent LOD, proper GPU retirement.
4. Complete compiled integration and fix any CMake/ObjC++/Vulkan/GLES errors before merging to main.
