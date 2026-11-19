# AIVERSE LENS Stage 6B — RAII model packet capture (2026-11-20)

## Commit and files
- Branch: `feature/lens-motionweave-integration`; main unchanged.
- `e642fdb5f0d3264a499c8a1db08a9ee796fee92d` and `105303e3fc82702e1f87be8568ace663d20d7684`: `engine/render/CameraModelSnapshot.h` `CaptureGuard`.
- `42722f01ba9aa82b3cbc3dd1e8abafd8676833b5`: `apps/client/GameClient.cpp` uses stack-only capture guard.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage6-model-snapshot-2026-11-20.md`.

## Architecture
- `CameraModelSnapshot::CaptureGuard` begins a per-frame synchronous `Renderer3D::ModelPacketTap` and removes it during explicit `end()` or destructor unwinding.
- GameClient creates one stack guard before `RtsView::draw`; starts it only when LENS is active, ends it after primary geometry/preview submissions, and releases automatically on exceptions. No heap allocation for the guard.
- Original primary renderer and MotionWeave update occur once. The recording payload is a bounded, deep-copied per-frame set of finalized ModelDraw packets, including skinned pose/matrices/baked sample/material overrides and attachments.
- This does not yet solve camera-specific **whole-model visual LOD** because final packets already reflect main-view LOD. Terrain/decals/fog/VFX/sprites are not in this snapshot.

## Tests actually run
- GitHub source inspection and remote commits only.
- **NOT RUN:** native compilation, CMake, CTest, Metal/Vulkan/GLES runtime or shader checks, editor/game integration smoke.
- CPU test `camera_model_snapshot` was authored in Stage 6 but not executed.

## Next
- Extract pre-LOD immutable visual packets from the universal View for per-camera LOD, while preserving animation update-once.
- Add tests asserting one animation clock advance and no duplicate events across N secondary camera passes; extend to terrain and other scene layers.
- Perform one consolidated compile/test/hardware pass, fix errors before merging into main.
